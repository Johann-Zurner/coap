/*
 * Einfacher CoAP-Button-Client für das nRF9160:
 *
 * - Stellt über LTE eine Verbindung ins Mobilfunknetz her.
 * - Öffnet einen UDP-Socket zu einem CoAP-Server (SERVER_IP, SERVER_PORT).
 * - Button 1 sendet einen CoAP GET Request.
 * - Button 2 sendet einen CoAP PUT Request mit dem Payload an die Ressource "echo".
 * - Zu jedem gesendeten CoAP-Paket wird der vollständige Inhalt als Hexdump geloggt.
 * - Eingehende CoAP-Antworten werden empfangen, als Hexdump ausgegeben
 *   und die Payload (falls vorhanden) als Text im Log angezeigt.
 *
 * Damit kann man per Knopfdruck CoAP-Nachrichten über LTE an den Server schicken
 * und Request/Response im Log (Hex + Payload) beobachten.
 */


#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/coap.h>
#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include <dk_buttons_and_leds.h>
#include <zephyr/logging/log.h>

#define SERVER_IP "81.173.152.150" 
#define SERVER_PORT 5683

#define COAP_BUF_SIZE 256

#define CONFIG_COAP_TOKEN_LEN 1

static int sock;
static struct sockaddr_in server_addr;

K_SEM_DEFINE(lte_connected, 0, 1);
static void lte_handler(const struct lte_lc_evt *const evt);
static void send_coap_put(void);
static void send_coap_put(void);
static void send_coap_get(void);
static void button_handler(uint32_t state, uint32_t changed);
static int modem_configure(void);
static void receive_coap_response(void);

LOG_MODULE_REGISTER(coap, LOG_LEVEL_DBG);

int main(void)
{

        int ret;
        LOG_INF("CoAP Button Client starting...\n");

        /* LTE */
        ret = modem_configure();
        if (ret)
        {
                LOG_ERR("Failed to configure the modem");
                return 0;
        }

        /* UDP socket */
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(SERVER_PORT);
        inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

        /* Buttons */
        dk_buttons_init(button_handler);

        LOG_INF("Ready. Press Button 1 → GET, Button 2 → PUT.\n");

        while (1)
        {
                k_sleep(K_FOREVER);
        }
}

static void send_coap_get(void)
{
        uint8_t buf[COAP_BUF_SIZE];
        struct coap_packet request;
        int len;

        coap_packet_init(&request,
                         buf,
                         sizeof(buf),
                         1,                     /* version */
                         COAP_TYPE_CON,         /* type */
                         CONFIG_COAP_TOKEN_LEN, /* token length */
                         coap_next_token(),     /* auto token */
                         COAP_METHOD_GET,       /* GET */
                         coap_next_id());       /* message ID */

        len = request.offset;

        sendto(sock, buf, len, 0,
               (struct sockaddr *)&server_addr,
               sizeof(server_addr));

        LOG_INF("Sending CoAP GET \n");
        LOG_HEXDUMP_DBG(buf, request.offset, "hex: " );
        LOG_DBG("Sent message ID: %d", coap_header_get_id(&request));

        receive_coap_response(); 

}
static void send_coap_put(void)
{
        uint8_t buf[COAP_BUF_SIZE];
        struct coap_packet request;
        const char payload[] = "123";
        int len;

        coap_packet_init(&request,
                         buf,
                         sizeof(buf),
                         1,
                         COAP_TYPE_CON,
                         CONFIG_COAP_TOKEN_LEN,
                         coap_next_token(),
                         COAP_METHOD_PUT,
                         coap_next_id());

        /* URI-Path: echo */
        coap_packet_append_option(&request,
                                  COAP_OPTION_URI_PATH,
                                  "echo", strlen("echo"));

        /* Payload marker + payload */
        coap_packet_append_payload_marker(&request);
        coap_packet_append_payload(&request,
                                   payload, strlen(payload));

        len = request.offset;

        sendto(sock, buf, len, 0,
               (struct sockaddr *)&server_addr,
               sizeof(server_addr));
        
        LOG_HEXDUMP_DBG(buf, request.offset, "Hex: ");
        LOG_DBG("Sent message ID: %d", coap_header_get_id(&request));
        LOG_INF("Payload: %s\n", payload);

        receive_coap_response();

}
static void button_handler(uint32_t state, uint32_t changed)
{
        if (changed & DK_BTN1_MSK)
        {
                if (state & DK_BTN1_MSK)
                {
                        send_coap_get();
                }
        }

        if (changed & DK_BTN2_MSK)
        {
                if (state & DK_BTN2_MSK)
                {
                        send_coap_put();
                }
        }
}

static void lte_handler(const struct lte_lc_evt *const evt)
{
        switch (evt->type)
        {
        case LTE_LC_EVT_NW_REG_STATUS:
                if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
                    (evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING))
                {
                        break;
                }
                LOG_DBG("Network registration status: %s",
                        evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ? "Connected - home network" : "Connected - roaming");
                k_sem_give(&lte_connected);
                break;
        case LTE_LC_EVT_RRC_UPDATE:
                LOG_DBG("RRC mode: %s", evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ? "Connected" : "Idle");
                break;
        default:
                break;
        }
}

static int modem_configure(void)
{
        int err;

        LOG_DBG("Initializing modem library");

        err = nrf_modem_lib_init();
        if (err)
        {
                LOG_ERR("Failed to initialize the modem library, error: %d", err);
                return err;
        }

        LOG_DBG("Connecting to LTE network");
        err = lte_lc_connect_async(lte_handler);
        if (err)
        {
                LOG_ERR("Error in lte_lc_connect_async, error: %d", err);
                return err;
        }

        k_sem_take(&lte_connected, K_FOREVER);
        LOG_DBG("Connected to LTE network");
        return 0;
}

static void receive_coap_response(void)
{
    uint8_t buf[COAP_BUF_SIZE];
    struct coap_packet reply;
    int ret;

    /* 1) Receive raw UDP / CoAP packet */
    ret = recv(sock, buf, sizeof(buf), 0);
    if (ret < 0) {
        LOG_ERR("recv() failed: %d", ret);
        return;
    }

    /* 2) Full message hex (header + token + options + payload) */
    LOG_HEXDUMP_INF(buf, ret, "RX CoAP FULL message:");

    /* 3) Parse CoAP */
    ret = coap_packet_parse(&reply, buf, ret, NULL, 0);
    if (ret < 0) {
        LOG_ERR("Failed to parse CoAP reply");
        return;
    }

    /* 5) Payload pointer + length (Zephyr API) */
    uint16_t payload_len = 0;
    const uint8_t *payload = coap_packet_get_payload(&reply, &payload_len);

    if (payload && payload_len > 0) {
        /* Payload only as text */
        LOG_INF("Payload (%u bytes): %.*s",
                payload_len, payload_len, payload);
    } else {
        LOG_INF("No payload in response");
    }
}
