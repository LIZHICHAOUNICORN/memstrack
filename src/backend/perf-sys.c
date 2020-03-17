#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <sys/sysinfo.h>

#include "perf-internal.h"
#include "../memstrack.h"

#define PERF_EVENTS_PATH "/sys/kernel/debug/tracing/events"
#define PERF_EVENTS_PATH_ALT "/sys/kernel/tracing/events"

int sys_perf_event_open(struct perf_event_attr *attr,
		int pid, int cpu, int group_fd,
		unsigned long flags)
{
	log_debug("Opening perf event on CPU: %d event: %lld\n", cpu, attr->config);
	return syscall(__NR_perf_event_open, attr, pid, cpu,
			group_fd, flags);
}

int perf_get_cpu_num(void) {
	return get_nprocs();
}

int perf_do_load_event_info(struct PerfEvent *event)
{
	char fmt_buffer[1024], *val;
	FILE *fmt_file;

	log_debug("\nValidating %s:%s\n", event->event_class, event->name);

	sprintf(fmt_buffer, "%s/%s/%s/format", PERF_EVENTS_PATH, event->event_class, event->name);
	fmt_file = fopen(fmt_buffer, "r");
	if (!fmt_file) {
		sprintf(fmt_buffer, "%s/%s/%s/format", PERF_EVENTS_PATH_ALT, event->event_class, event->name);
		fmt_file = fopen(fmt_buffer, "r");
	}

	if (!fmt_file) {
		log_error("Failed to open %s\n", event->name);
		log_error("Make sure debugfs is mounted and have the right permission\n");
		return 1;
	}

	/* Parse in order name, ID, format */
	fgets(fmt_buffer, 1024, fmt_file);
	val = strstr(fmt_buffer, "name: ") + 6;
	if (val) {
		if (strncmp(val, event->name, strlen(event->name))) {
			log_error("Expecting event name %s but got %s\n", event->name, val);
			return 1;
		}
	} else {
		log_error("Failed to verify event name of %s\n", event->name);
		return 1;
	}

	fgets(fmt_buffer, 1024, fmt_file);
	val = strstr(fmt_buffer, "ID:");
	if (val) {
		if (!sscanf(fmt_buffer + 3, "%d", &event->id)) {
			log_error("Failed to parse event id of %s\n", event->name);
			return 1;
		}
	} else {
		log_error("Failed to get event id of %s\n", event->name);
		return 1;
	}
	log_debug("ID of %s:%s is %d\n", event->event_class, event->name, event->id);

	fgets(fmt_buffer, 1024, fmt_file);
	if (strncmp(fmt_buffer, "format:", 7)) {
		log_error("Failed to parse event format of %s\n", event->name);
		return 1;
	}

	while (fgets(fmt_buffer, 1024, fmt_file)) {
		val = strstr(fmt_buffer, "field:");
		if (!val) {
			continue;
		} else {
			val += 6;
		}

		/* x <= 32 : x is space or tab or any non-alpha non-number ascii */
		char *type = strtok(val, ";");
		char *name = type + strlen(type) - 1; while(*(--name) > 32){};(name++)[0] = 0;
		char *offset = strtok(NULL, ";"); while(*(++offset) < '0' || *offset > '9'){};
		char *size = strtok(NULL, ";"); while(*(++size) < '0' || *size > '9'){};
		char *is_signed = strtok(NULL, ";"); while(*(++is_signed) < '0' || *is_signed > '9'){};

		struct PerfEventField *field = NULL;

		for (int i = 0; i < event->fileds_num; ++i) {
			if(!strcmp(name, event->fields[i].name)) {
				field = event->fields + i;
				break;
			}
		}

		if (!field) {
			log_debug("Ignored field %s\n", name);
		} else {
			if (strcmp(type, field->type)) {
				log_error("Expected type %s:%s type %s but got %s\n", event->name, field->name, field->type, type);
			}

			int _size;
			sscanf(offset, "%hd", &field->offset);
			sscanf(size, "%d", &_size);
			sscanf(is_signed, "%hd", &field->is_signed);

			if (_size != field->size) {
				log_error("Expected type %s:%s size %hd but got %d\n", event->name, field->name, field->size, _size);
				field->size = _size;
			}

			field->checked = 1;

			log_debug("format: %s: offset: %hd, size: %hd, is_signed: %hd\n",
					field->name, field->offset, field->size, field->is_signed);
		}
	}

	return 0;
}