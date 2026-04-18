#ifndef MOTON_CONTROL_SERVICES_REGISTER_H
#define MOTON_CONTROL_SERVICES_REGISTER_H

#include <zephyr/sys/iterable_sections.h>
#include <zephyr/data/json.h>

typedef int (*motion_control_service_func_t)(void *service_data);

struct motion_control_service {
	const char *name;
	motion_control_service_func_t func;
	const void *service_descriptor;
	size_t service_descriptor_size;
};

int motion_control_services_register_notify(const char *service_name);

#define REGISTER_MOTION_CONTROL_SERVICE(service, _name, _func, _service_descriptor,                \
					_service_descriptor_size)                                  \
	STRUCT_SECTION_ITERABLE(motion_control_service, service) = {                               \
		.name = _name,                                                                     \
		.func = _func,                                                                     \
		.service_descriptor = _service_descriptor,                                         \
		.service_descriptor_size = _service_descriptor_size,                               \
	};

#endif
