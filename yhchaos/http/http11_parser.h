#ifndef http11_parser_h
#define http11_parser_h

#include "http11_common.h"
//如何将http的报文解析到http.h中的那两个结构体中，这个是解析请求的
typedef struct http_parser { 
  //全部初始化为0
  int cs;
  size_t body_start;
  int content_len;
  size_t nread;
  size_t mark;
  size_t field_start;
  size_t field_len;
  size_t query_start;
  int xml_sent;
  int json_sent;
  //下面的参数默认初始化,设置为http_parser.h的HttpRspParser,httpReqParser对象，将解析出来的数据放到http.h中httpReq和httpRsp对象m_data中
  void *data;
/*
at和field是http原文，将原文解析到data中，其中at是解析完的数据，field是解析完的数据的字段
typedef void (*element_cb)(void *data, const char *at, size_t length);
typedef void (*field_cb)(void *data, const char *field, size_t flen, const char *value, size_t vlen);
*/
  int uri_relaxed;
  field_cb http_field;
  element_cb request_method;
  element_cb request_uri;
  element_cb fragment;
  element_cb request_path;
  element_cb query_string;
  element_cb http_version;
  element_cb header_done;
  
} http_parser;
//初始化http_parser,初始化cs到json_sent
int http_parser_init(http_parser *parser);
//判断是否已经解析结束
int http_parser_finish(http_parser *parser);
//进行http报文的解析，用http_parser来解析http报文，解析的数据是data，长度是len，off是开始解析的data偏移量
size_t http_parser_execute(http_parser *parser, const char *data, size_t len, size_t off);
//解析的http_parser是否有错误
int http_parser_has_error(http_parser *parser);
int http_parser_is_finished(http_parser *parser);

#define http_parser_nread(parser) (parser)->nread 

#endif
