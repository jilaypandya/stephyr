/*
 * MQTT Publisher-Subscriber Client
 * Combines publishing and subscribing functionality
 * Uses sys_init for automatic thread startup at APPLICATION level
 * Static IP configuration only
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mqtt_pubsub_client, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/random/random.h>
#include <zephyr/init.h>
#include <zephyr/posix/poll.h>
#include <zephyr/posix/arpa/inet.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/net/net_if.h>
#include <app/subsys/mqtt_client.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>

#include <string.h>
#include <errno.h>

/* Configuration */
#define SERVER_ADDR "169.254.191.55"  // Change to your MQTT broker IP
#define SERVER_PORT 1883
#define MQTT_CLIENTID "zephyr_pubsub_client"

/* Static IP Configuration */
#define MY_IP_ADDR "169.254.191.50"
#define MY_NETMASK "255.255.255.0"
#define MY_GATEWAY "169.254.191.1"

#define APP_MQTT_BUFFER_SIZE 512
#define APP_CONNECT_TRIES 10
#define APP_CONNECT_TIMEOUT_MS 10000
#define APP_SLEEP_MSECS 5000

#define PUBLISH_TOPIC "zephyr/sensors/temperature"
#define SUBSCRIBE_TOPIC "motion_control/commands/#"

/* Thread configuration */
#define PUBSUB_THREAD_STACK_SIZE 4096
#define PUBSUB_THREAD_PRIORITY 7

/* Buffers for MQTT client */
static uint8_t rx_buffer[APP_MQTT_BUFFER_SIZE];
static uint8_t tx_buffer[APP_MQTT_BUFFER_SIZE];

/* Payload buffer for incoming messages */
static uint8_t payload_buffer[256];

/* The mqtt client struct */
static struct mqtt_client client_ctx;

/* MQTT Broker details */
static struct sockaddr_storage broker;

static struct pollfd fds[1];
static int nfds;

static bool connected;

/* Thread stack and control block */
static K_THREAD_STACK_DEFINE(pubsub_thread_stack, PUBSUB_THREAD_STACK_SIZE);
static struct k_thread pubsub_thread_data;
static k_tid_t pubsub_thread_id;

/* Function prototypes */
static void prepare_fds(struct mqtt_client *client);
static void clear_fds(void);
static int wait(int timeout);
static void pubsub_thread_entry(void *p1, void *p2, void *p3);

mqtt_client_listener_t mqtt_client_listener = NULL;

/* linked list : async */
void mqtt_client_attach_listener(mqtt_client_listener_t listener) {
	mqtt_client_listener = listener;
}

/* Test network connectivity by pinging the broker */
static int test_broker_connectivity(void)
{
	int sock;
	struct sockaddr_in test_addr;
	int ret;

	LOG_INF("Testing connectivity to broker %s:%d...", SERVER_ADDR, SERVER_PORT);

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		LOG_ERR("Failed to create test socket: %d", errno);
		return -errno;
	}

	memset(&test_addr, 0, sizeof(test_addr));
	test_addr.sin_family = AF_INET;
	test_addr.sin_port = htons(SERVER_PORT);
	inet_pton(AF_INET, SERVER_ADDR, &test_addr.sin_addr);

	/* Set socket timeout */
	struct timeval timeout;
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

	ret = connect(sock, (struct sockaddr *)&test_addr, sizeof(test_addr));
	if (ret < 0) {
		LOG_ERR("Cannot reach broker: %d (%s)", errno, strerror(errno));
		close(sock);
		return -errno;
	}

	LOG_INF("Broker is reachable!");
	close(sock);
	return 0;
}

/* Setup static IP address */
static int setup_static_ip(void)
{
	struct net_if *iface = net_if_get_default();
	struct in_addr addr, gateway;
	struct in_addr netmask;

	if (!iface) {
		LOG_ERR("No network interface found");
		return -ENODEV;
	}

	LOG_INF("Configuring static IP...");

	/* Parse IP address */
	if (inet_pton(AF_INET, MY_IP_ADDR, &addr) != 1) {
		LOG_ERR("Invalid IP address: %s", MY_IP_ADDR);
		return -EINVAL;
	}

	/* Parse netmask */
	if (inet_pton(AF_INET, MY_NETMASK, &netmask) != 1) {
		LOG_ERR("Invalid netmask: %s", MY_NETMASK);
		return -EINVAL;
	}

	/* Parse gateway */
	if (inet_pton(AF_INET, MY_GATEWAY, &gateway) != 1) {
		LOG_ERR("Invalid gateway: %s", MY_GATEWAY);
		return -EINVAL;
	}

	/* Set IP address with netmask */
	struct net_if_addr *ifaddr = net_if_ipv4_addr_add(iface, &addr,
							   NET_ADDR_MANUAL, 0);
	if (!ifaddr) {
		LOG_ERR("Failed to add IP address");
		return -EINVAL;
	}

	/* Set netmask by configuring the prefix length */
	net_if_ipv4_set_netmask_by_addr(iface, &addr, &netmask);

	/* Set gateway */
	net_if_ipv4_set_gw(iface, &gateway);

	LOG_INF("Network configuration:");
	LOG_INF("  Client IP Address: %s", MY_IP_ADDR);
	LOG_INF("  Netmask: %s", MY_NETMASK);
	LOG_INF("  Gateway: %s", MY_GATEWAY);

	return 0;
}

/* Print network interface details */
static void print_network_info(void)
{
	struct net_if *iface = net_if_get_default();
	char addr_str[NET_IPV4_ADDR_LEN];

	if (!iface) {
		LOG_ERR("No network interface");
		return;
	}

	LOG_INF("Network Interface Information:");
	LOG_INF("  Interface: %p", iface);
	LOG_INF("  Link up: %s", net_if_is_up(iface) ? "YES" : "NO");

	struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;
	if (ipv4) {
		for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
			if (!net_ipv4_is_addr_unspecified(&ipv4->unicast[i].ipv4.address.in_addr)) {
				inet_ntop(AF_INET, &ipv4->unicast[i].ipv4.address.in_addr,
					  addr_str, sizeof(addr_str));
				LOG_INF("  IPv4 Address %d: %s", i, addr_str);
			}
		}

		if (ipv4->gw.s_addr != 0) {
			inet_ntop(AF_INET, &ipv4->gw, addr_str, sizeof(addr_str));
			LOG_INF("  Gateway: %s", addr_str);
		}
	}
}

/* Wait for network interface to be ready and configure IP */
static int wait_for_network(void)
{
	struct net_if *iface = net_if_get_default();
	int retries = 50;
	int ret;

	if (!iface) {
		LOG_ERR("No network interface found");
		return -ENODEV;
	}

	LOG_INF("Waiting for network interface to be up...");

	/* Wait for interface to be up */
	while (retries--) {
		if (net_if_is_up(iface)) {
			LOG_INF("Network interface is up");
			break;
		}
		k_sleep(K_MSEC(100));
	}

	if (retries <= 0) {
		LOG_ERR("Network interface timeout");
		return -ETIMEDOUT;
	}

	/* Setup static IP */
	ret = setup_static_ip();
	if (ret != 0) {
		return ret;
	}

	/* Wait for network to stabilize */
	k_sleep(K_SECONDS(2));

	/* Print network info for debugging */
	print_network_info();

	/* Test broker connectivity */
	ret = test_broker_connectivity();
	if (ret != 0) {
		LOG_WRN("Broker connectivity test failed, but will try MQTT connection anyway");
	}

	LOG_INF("Network is ready");
	return 0;
}

/* Prepare file descriptors for polling */
static void prepare_fds(struct mqtt_client *client)
{
	fds[0].fd = client->transport.tcp.sock;
	fds[0].events = POLLIN;
	nfds = 1;
}

static void clear_fds(void)
{
	nfds = 0;
}

static int wait(int timeout)
{
	int ret = 0;

	if (nfds > 0) {
		ret = poll(fds, nfds, timeout);
		if (ret < 0) {
			LOG_ERR("poll error: %d", errno);
		}
	}

	return ret;
}

/* Handle incoming PUBLISH messages */
static int handle_publish(struct mqtt_client *client, const struct mqtt_evt *evt)
{
	const struct mqtt_publish_param *p = &evt->param.publish;
	int ret;
	size_t payload_len;

	LOG_INF("PUBLISH received:");
	LOG_INF("  Topic: %.*s", p->message.topic.topic.size,
		p->message.topic.topic.utf8);
	LOG_INF("  QoS: %d", p->message.topic.qos);
	LOG_INF("  Payload length: %d", p->message.payload.len);

	payload_len = p->message.payload.len;

	/* Read the payload */
	if (payload_len > 0) {
		ret = mqtt_read_publish_payload(client, payload_buffer, payload_len);
		if (ret < 0) {
			LOG_ERR("Failed to read payload");
			return ret;
		}

		/* Null-terminate and print if it's text */
		if (ret < sizeof(payload_buffer)) {
			payload_buffer[ret] = '\0';
			LOG_INF(" Payload: %s", payload_buffer);
		}
		if (mqtt_client_listener != NULL) {
			mqtt_client_listener(p->message.topic.topic.utf8, payload_buffer);
		}
	} else {
		LOG_INF("  Payload: (empty)");
	}

	return 0;
}

/* MQTT event handler */
void mqtt_evt_handler(struct mqtt_client *const client,
		      const struct mqtt_evt *evt)
{
	int err;

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT connect failed %d", evt->result);
			break;
		}

		connected = true;
		LOG_INF("MQTT client connected!");
		break;

	case MQTT_EVT_DISCONNECT:
		LOG_INF("MQTT client disconnected %d", evt->result);
		connected = false;
		clear_fds();
		break;

	case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBACK error %d", evt->result);
			break;
		}
		LOG_INF("PUBACK packet id: %u", evt->param.puback.message_id);
		break;

	case MQTT_EVT_PUBREC:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBREC error %d", evt->result);
			break;
		}
		LOG_INF("PUBREC packet id: %u", evt->param.pubrec.message_id);

		const struct mqtt_pubrel_param rel_param = {
			.message_id = evt->param.pubrec.message_id
		};

		err = mqtt_publish_qos2_release(client, &rel_param);
		if (err != 0) {
			LOG_ERR("Failed to send MQTT PUBREL: %d", err);
		}
		break;

	case MQTT_EVT_PUBCOMP:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBCOMP error %d", evt->result);
			break;
		}
		LOG_INF("PUBCOMP packet id: %u", evt->param.pubcomp.message_id);
		break;

	case MQTT_EVT_SUBACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT SUBACK error %d", evt->result);
			break;
		}
		LOG_INF("SUBACK packet id: %u", evt->param.suback.message_id);
		break;

	case MQTT_EVT_PUBLISH:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBLISH error %d", evt->result);
			break;
		}

		handle_publish(client, evt);

		/* Send PUBACK for QoS 1, PUBREC for QoS 2 */
		if (evt->param.publish.message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
			const struct mqtt_puback_param ack = {
				.message_id = evt->param.publish.message_id
			};
			mqtt_publish_qos1_ack(client, &ack);
		} else if (evt->param.publish.message.topic.qos == MQTT_QOS_2_EXACTLY_ONCE) {
			const struct mqtt_pubrec_param rec = {
				.message_id = evt->param.publish.message_id
			};
			mqtt_publish_qos2_receive(client, &rec);
		}
		break;

	case MQTT_EVT_PUBREL:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBREL error %d", evt->result);
			break;
		}
		LOG_INF("PUBREL packet id: %u", evt->param.pubrel.message_id);

		const struct mqtt_pubcomp_param comp = {
			.message_id = evt->param.pubrel.message_id
		};
		mqtt_publish_qos2_complete(client, &comp);
		break;

	case MQTT_EVT_PINGRESP:
		LOG_INF("PINGRESP packet");
		break;

	default:
		LOG_INF("Unhandled MQTT event type: %d", evt->type);
		break;
	}
}

/* Initialize broker address */
static void broker_init(void)
{
	struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;

	memset(&broker, 0, sizeof(broker));
	broker4->sin_family = AF_INET;
	broker4->sin_port = htons(SERVER_PORT);

	int ret = inet_pton(AF_INET, SERVER_ADDR, &broker4->sin_addr);
	if (ret != 1) {
		LOG_ERR("Invalid broker address: %s", SERVER_ADDR);
	} else {
		LOG_INF("Broker configured: %s:%d", SERVER_ADDR, SERVER_PORT);
	}
}

/* Initialize MQTT client */
static void client_init(struct mqtt_client *client)
{
	/* Clear the client structure */
	memset(client, 0, sizeof(*client));

	mqtt_client_init(client);

	broker_init();

	/* MQTT client configuration */
	client->broker = &broker;
	client->evt_cb = mqtt_evt_handler;
	client->client_id.utf8 = (uint8_t *)MQTT_CLIENTID;
	client->client_id.size = strlen(MQTT_CLIENTID);
	client->password = NULL;
	client->user_name = NULL;
	client->protocol_version = MQTT_VERSION_3_1_1;

	/* MQTT buffers configuration */
	client->rx_buf = rx_buffer;
	client->rx_buf_size = sizeof(rx_buffer);
	client->tx_buf = tx_buffer;
	client->tx_buf_size = sizeof(tx_buffer);

	/* MQTT transport configuration */
	client->transport.type = MQTT_TRANSPORT_NON_SECURE;

	/* Keep alive configuration */
	client->keepalive = 60U;
	client->clean_session = 1U;

	LOG_INF("MQTT client initialized:");
	LOG_INF("  Client ID: %s", MQTT_CLIENTID);
	LOG_INF("  Keep alive: %d seconds", client->keepalive);
	LOG_INF("  Protocol version: MQTT 3.1.1");
}

/* Subscribe to topics */
static int subscribe(struct mqtt_client *client)
{
	struct mqtt_topic topics[] = {
		{
			.topic.utf8 = SUBSCRIBE_TOPIC,
			.topic.size = strlen(SUBSCRIBE_TOPIC),
			.qos = MQTT_QOS_1_AT_LEAST_ONCE
		}
	};

	const struct mqtt_subscription_list sub_list = {
		.list = topics,
		.list_count = ARRAY_SIZE(topics),
		.message_id = sys_rand16_get()
	};

	LOG_INF("Subscribing to: %s", SUBSCRIBE_TOPIC);

	return mqtt_subscribe(client, &sub_list);
}

/* Publish message */
static int publish(struct mqtt_client *client, const char *data, enum mqtt_qos qos)
{
	struct mqtt_publish_param param;

	param.message.topic.qos = qos;
	param.message.topic.topic.utf8 = (uint8_t *)PUBLISH_TOPIC;
	param.message.topic.topic.size = strlen(PUBLISH_TOPIC);
	param.message.payload.data = (uint8_t *)data;
	param.message.payload.len = strlen(data);
	param.message_id = sys_rand16_get();
	param.dup_flag = 0U;
	param.retain_flag = 0U;

	LOG_INF("Publishing to %s: %s", PUBLISH_TOPIC, data);

	return mqtt_publish(client, &param);
}

/* Connect to MQTT broker */
static int try_to_connect(struct mqtt_client *client)
{
	int rc, i = 0;

	while (i++ < APP_CONNECT_TRIES && !connected) {
		client_init(client);

		LOG_INF("Attempting MQTT connection (attempt %d/%d)...", i, APP_CONNECT_TRIES);

		rc = mqtt_connect(client);
		if (rc != 0) {
			LOG_ERR("mqtt_connect failed: %d (%s)", rc, strerror(-rc));
			k_sleep(K_MSEC(APP_SLEEP_MSECS));
			continue;
		}

		prepare_fds(client);

		LOG_INF("Waiting for CONNACK...");
		if (wait(APP_CONNECT_TIMEOUT_MS)) {
			rc = mqtt_input(client);
			if (rc != 0) {
				LOG_ERR("mqtt_input failed: %d", rc);
			}
		}

		if (!connected) {
			LOG_WRN("Connection attempt timed out, aborting...");
			mqtt_abort(client);
		}
	}

	if (connected) {
		return 0;
	}

	return -EINVAL;
}

/* Process MQTT messages and sleep */
static int process_mqtt_and_sleep(struct mqtt_client *client, int timeout)
{
	int64_t remaining = timeout;
	int64_t start_time = k_uptime_get();
	int rc;

	while (remaining > 0 && connected) {
		if (wait(remaining)) {
			rc = mqtt_input(client);
			if (rc != 0) {
				LOG_ERR("%s mqtt_input error: %d", __func__, rc);
				return rc;
			}
		}

		rc = mqtt_live(client);
		if (rc != 0 && rc != -EAGAIN) {
			LOG_ERR("mqtt_live error: %d", rc);
			return rc;
		} else if (rc == 0) {
			rc = mqtt_input(client);
			if (rc != 0) {
				LOG_ERR("mqtt_input error: %d", rc);
				return rc;
			}
		}

		remaining = timeout + start_time - k_uptime_get();
	}

	return 0;
}

/* Main publisher-subscriber loop */
static int pubsub_loop(void)
{
	int rc;
	char payload[64];
	int counter = 0;

	/* Wait for network to be ready */
	rc = wait_for_network();
	if (rc != 0) {
		LOG_ERR("Network not ready: %d", rc);
		return rc;
	}

	LOG_INF("Attempting to connect to MQTT broker...");
	rc = try_to_connect(&client_ctx);
	if (rc != 0) {
		LOG_ERR("Connection failed: %d", rc);
		LOG_ERR("Please check:");
		LOG_ERR("  1. MQTT broker is running at %s:%d", SERVER_ADDR, SERVER_PORT);
		LOG_ERR("  2. Network connectivity (can you ping the broker?)");
		LOG_ERR("  3. Firewall settings on broker");
		LOG_ERR("  4. Client IP %s is on same subnet as broker", MY_IP_ADDR);
		return rc;
	}

	/* Subscribe to topics after connection */
	rc = subscribe(&client_ctx);
	if (rc != 0) {
		LOG_ERR("Subscribe failed: %d", rc);
		return rc;
	}

	/* Wait for SUBACK */
	rc = process_mqtt_and_sleep(&client_ctx, APP_SLEEP_MSECS);
	if (rc != 0) {
		return rc;
	}

	/* Main loop: publish and receive messages */
	while (connected) {
		/* Send ping */
		rc = mqtt_ping(&client_ctx);
		if (rc != 0) {
			LOG_ERR("mqtt_ping failed: %d", rc);
			break;
		}

		rc = process_mqtt_and_sleep(&client_ctx, APP_SLEEP_MSECS);
		if (rc != 0) {
			break;
		}

		/* Publish temperature data */
		snprintf(payload, sizeof(payload),
			 "{\"temperature\": %d, \"counter\": %d}",
			 20 + (sys_rand8_get() % 15), counter++);

		rc = publish(&client_ctx, payload, MQTT_QOS_1_AT_LEAST_ONCE);
		if (rc != 0) {
			LOG_ERR("mqtt_publish failed: %d", rc);
			break;
		}

		/* Process incoming messages */
		rc = process_mqtt_and_sleep(&client_ctx, APP_SLEEP_MSECS);
		if (rc != 0) {
			break;
		}
	}

	/* Disconnect */
	rc = mqtt_disconnect(&client_ctx, 0);
	LOG_INF("Disconnected: %d", rc);

	return 0;
}

/* Thread entry point */
static void pubsub_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("MQTT Publisher-Subscriber thread started");

	/* Run the pubsub loop */
	pubsub_loop();

	LOG_INF("MQTT Publisher-Subscriber thread exiting");
}

/* Initialization function called at APPLICATION level */
static int mqtt_pubsub_init(void)
{
	LOG_INF("Initializing MQTT Publisher-Subscriber");

	/* Create and start the thread */
	pubsub_thread_id = k_thread_create(&pubsub_thread_data,
					   pubsub_thread_stack,
					   K_THREAD_STACK_SIZEOF(pubsub_thread_stack),
					   pubsub_thread_entry,
					   NULL, NULL, NULL,
					   PUBSUB_THREAD_PRIORITY,
					   0,
					   K_NO_WAIT);

	if (!pubsub_thread_id) {
		LOG_ERR("Failed to create MQTT pubsub thread");
		return -ENOMEM;
	}

	k_thread_name_set(pubsub_thread_id, "mqtt_pubsub");

	LOG_INF("MQTT Publisher-Subscriber thread created successfully");

	return 0;
}

/* Register initialization at APPLICATION level */
SYS_INIT(mqtt_pubsub_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
