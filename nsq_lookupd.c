#include <php.h>
#include "php_nsq.h"
#include "nsq_lookupd.h"

#include "event2/http.h"
#include "event2/http_struct.h"
#include "event2/event.h"
#include "event2/buffer.h"
#include "event2/dns.h"
#include "event2/thread.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/queue.h>
#include <event.h>

static zend_class_entry *nsq_lookupd_ce;

static const zend_function_entry nsq_lookupd_functions[] = {
	PHP_FE_END	/* Must be the last line in nsq_functions[] */

};
void lookupd_init(){
    zend_class_entry nsq_lookupd;
    INIT_CLASS_ENTRY(nsq_lookupd,"NsqLookupd",nsq_lookupd_functions);
    //nsq_lookupd_ce = zend_register_internal_class_ex(&nsq_lookupd,NULL,NULL TSRMLS_CC);
    nsq_lookupd_ce = zend_register_internal_class(&nsq_lookupd TSRMLS_CC);
    zend_declare_property_null(nsq_lookupd_ce,ZEND_STRL("address"),ZEND_ACC_PUBLIC TSRMLS_CC);
}



void RemoteReadCallback(struct evhttp_request* remote_rsp, void* arg)
{
    event_base_loopexit((struct event_base*)arg, NULL);
} 


void ReadChunkCallback(struct evhttp_request* remote_rsp, void* arg)
{
    char buf[4096];
    struct evbuffer* evbuf = evhttp_request_get_input_buffer(remote_rsp);
    int n = 0;
    while ((n = evbuffer_remove(evbuf, buf, 4096)) > 0)
    {
        fwrite(buf, n, 1, stdout);
    }
}

void RemoteRequestErrorCallback(enum evhttp_request_error error, void* arg)
{
    fprintf(stderr, "request failed\n");
    event_base_loopexit((struct event_base*)arg, NULL);
}

void RemoteConnectionCloseCallback(struct evhttp_connection* connection, void* arg)
{
    fprintf(stderr, "remote connection closed\n");
    event_base_loopexit((struct event_base*)arg, NULL);
}

char* lookup(char *host, char* topic){
    char * url = emalloc(sizeof(host) + sizeof(topic) + 20);
    sprintf(url, "%s%s", host, "lookupd", "\n");
    if(strstr(url,"http://")){
        sprintf(url, "%s%s%s", host, "/lookup?topic=", topic);
    }else{
        sprintf(url, "%s%s%s%s", "http://", host, "/lookup?topic=", topic); 
    }
    char *data =  request(url);
    printf("data:%s",data);
	efree(url);
    return data;

}

char* request(char* url)
{
    printf("url:%s",url);
    char * msg = emalloc(120) ;
    struct evhttp_uri* uri = evhttp_uri_parse(url);
    if (!uri)
    {
        fprintf(stderr, "parse url failed!\n");
        msg =  "{\"message\":\"parse url failed!\"}";
        return msg;
    }

    struct event_base* base = event_base_new();
    if (!base)
    {
        fprintf(stderr, "create event base failed!\n");
        msg =  "{\"message\":\"create event base failed!\"}";
        return msg;
    }

    struct evdns_base* dnsbase = evdns_base_new(base, 1);
    if (!dnsbase)
    {
        fprintf(stderr, "create dns base failed!\n");
        msg =  "{\"message\":\"create dns base failed!\"}";
        return msg;
    }
    assert(dnsbase);

    struct evhttp_request* request = evhttp_request_new(RemoteReadCallback, base);
    //evhttp_request_set_header_cb(request, ReadHeaderDoneCallback);
    evhttp_request_set_chunked_cb(request, ReadChunkCallback);
    evhttp_request_set_error_cb(request, RemoteRequestErrorCallback);

    const char* host = evhttp_uri_get_host(uri);
    if (!host)
    {
        fprintf(stderr, "parse host failed!\n");
        msg = "{\"message\":\"stderr, parse host failed!\"}";
        return msg;
    }

	
    int port = evhttp_uri_get_port(uri);
    if (port < 0) port = 80;

    const char* request_url = url;
    const char* path = evhttp_uri_get_path(uri);
    if (path == NULL || strlen(path) == 0)
    {
        request_url = "/";
    }

    //printf("url:%s host:%s port:%d path:%s request_url:%s\n", url, host, port, path, request_url);

    struct evhttp_connection* connection =  evhttp_connection_base_new(base, dnsbase, host, port);
    if (!connection)
    {
        fprintf(stderr, "create evhttp connection failed!\n");
        msg =  "{\"message\":\"create evhttp connection failed!\"}";
        return msg;
    }

    evhttp_connection_set_closecb(connection, RemoteConnectionCloseCallback, base);

    evhttp_add_header(evhttp_request_get_output_headers(request), "Host", host);
    evhttp_make_request(connection, request, EVHTTP_REQ_GET, request_url);

    event_base_dispatch(base);

    char *buf = malloc(4096*sizeof(char));
    struct evbuffer* evbuf = evhttp_request_get_input_buffer(request);
    int n = 0;
    while ((n = evbuffer_remove(evbuf, buf, 4096)) > 0)
    {
        fwrite(buf, n, 1, stdout);
    }
    return buf;
}
