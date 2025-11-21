root@DTLSserver:~/coap_server# cat coapserver.c
/* Minimal CoAP UDP echo server (libcoap-3) */

#include <stdio.h>
#include <string.h>
#include <coap3/coap.h>
#include <coap3/coap_pdu.h>

/* Echo handler: send back the request payload */
static void
hnd_echo(coap_resource_t *resource,
         coap_session_t *session,
         const coap_pdu_t *request,
         const coap_string_t *query,
         coap_pdu_t *response) {

    size_t size;
    const uint8_t *data;

    /* Response code 2.05 Content */
    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT);

    /* Get payload from request */
    if (coap_get_data(request, &size, &data) && size > 0) {

        /* Payload as text */
        printf("Received payload (%zu bytes): \"", size);
        printf("%.*s", (int)size, (const char *)data);
        printf("\"\n");

        /* Payload as hex */
        printf("Payload hex: ");
        for (size_t i = 0; i < size; i++) {
            printf("%02X ", data[i]);
        }
        printf("\n");

        /* Build reply: <payload> + " zurück" */
        const char *suffix = " zurück";
        size_t suffix_len = strlen(suffix);

        /* Simple fixed-size buffer for response */
        uint8_t out[512];
        size_t out_len = size + suffix_len;
        if (out_len > sizeof(out)) {
            out_len = sizeof(out); /* brutale Kürzung, aber egal für Demo */
        }

        /* Copy original payload */
        memcpy(out, data, size);

        /* Append suffix (may be truncated in extreme cases) */
        size_t max_suffix = out_len - size;
        memcpy(out + size, suffix, max_suffix);

        /* Send combined payload back */
        coap_add_data(response, out_len, out);

    } else {
        /* No payload: say so */
        const char *msg = "no payload";
        printf("Received request with NO payload\n");
        coap_add_data(response, strlen(msg), (const uint8_t *)msg);
    }
}

static void
hnd_root(coap_resource_t *resource,
           coap_session_t *session,
           const coap_pdu_t *request,
           const coap_string_t *query,
           coap_pdu_t *response)
{
    const char *msg = "Rumpelstilzchen";

    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT);
    coap_add_data(response, strlen(msg), (const uint8_t *)msg);

    printf("GET request on /hello → responded with \"Rumpel!\"\n");
}


int main(void) {
    coap_context_t  *ctx;
    coap_address_t   addr;
    coap_endpoint_t *ep;
    coap_resource_t *res;

    coap_startup();

    /* Listen on 0.0.0.0:5683 (UDP) */
    coap_address_init(&addr);
    addr.addr.sin.sin_family      = AF_INET;
    addr.addr.sin.sin_addr.s_addr = INADDR_ANY;
    addr.addr.sin.sin_port        = htons(5683);

    ctx = coap_new_context(NULL);
    ep  = coap_new_endpoint(ctx, &addr, COAP_PROTO_UDP);

    /* /echo resource */
    res = coap_resource_init(coap_make_str_const("echo"), 0);

    /* Echo on GET, PUT, POST */
    coap_register_handler(res, COAP_REQUEST_GET,  hnd_echo);
    coap_register_handler(res, COAP_REQUEST_PUT,  hnd_echo);
    coap_register_handler(res, COAP_REQUEST_POST, hnd_echo);

    coap_add_resource(ctx, res);

    coap_resource_t *res_hello;

    res_hello = coap_resource_init(coap_make_str_const(""), 0);
    coap_register_handler(res_hello, COAP_REQUEST_GET, hnd_root);
    coap_add_resource(ctx, res_hello);

   /* Main loop – server stays up, handles many clients */
    while (1) {
        coap_io_process(ctx, COAP_IO_WAIT);
    }

    coap_free_context(ctx);
    coap_cleanup();
    return 0;
}
