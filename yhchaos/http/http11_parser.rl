/**
 *
 * Copyright (c) 2010, Zed A. Shaw and Mongrel2 Project Contributors.
 * All rights reserved.
 * 
 * CppRedistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * CppRedistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 * 
 *     * CppRedistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 * 
 *     * Neither the name of the Mongrel2 Project, Zed A. Shaw, nor the names
 *       of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "http11_parser.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
//#include <dbg.h>

#define LEN(AT, FPC) (FPC - buffer - parser->AT)
#define MARK(M,FPC) (parser->M = (FPC) - buffer)
#define PTR_TO(F) (buffer + parser->F)

/** Machine **/
/**
这段代码看起来是一个基于 Ragel 的状态机定义，用于解析 HTTP 请求。Ragel 是一个用于生成状态机代码的工具，通常用于创建词法分析器和解析器等。

以下是这段代码的主要部分和功能：

1. `%%{ ... }%%` 之间的部分包含了状态机的定义和操作。

2. 通过 `machine http_parser;` 定义了一个名为 `http_parser` 的状态机。

3. 定义了一系列的动作（action），这些动作会在状态机的不同状态下执行。例如，`mark` 动作用于标记当前位置。

4. 根据 HTTP 请求的不同部分，定义了多个动作，如 `request_method`、`request_uri`、`http_version` 等。这些动作会在解析对应部分时执行，通常用于将解析结果传递给其他处理逻辑。

5. 定义了 HTTP 协议的语法规则，包括 URI 的解析规则、请求行、头部字段等。这些规则用于指导状态机的行为，以正确解析 HTTP 请求。

6. 最后，通过 `main := (Req | SockReq) @done;` 定义了状态机的入口点和结束条件。

这段代码是 Ragel 状态机的描述，用于定义 HTTP 请求的解析逻辑。它包含了各种状态、状态迁移以及在不同状态下执行的动作。这些定义会被 Ragel 工具处理，生成对应的状态机代码，以便在程序中使用。要使用这个状态机，您需要将其与 Ragel 工具一起使用，然后将生成的代码与您的项目一起编译并集成到您的 HTTP 请求解析逻辑中。
**/
%%{
  
  machine http_parser;

  action mark {MARK(mark, fpc); }


  action start_field { MARK(field_start, fpc); }
  action write_field { 
    parser->field_len = LEN(field_start, fpc);
  }

  action start_value { MARK(mark, fpc); }

  action write_value {
    if(parser->http_field != NULL) {
      parser->http_field(parser->data, PTR_TO(field_start), parser->field_len, PTR_TO(mark), LEN(mark, fpc));
    }
  }

  action request_method { 
    if(parser->request_method != NULL) 
      parser->request_method(parser->data, PTR_TO(mark), LEN(mark, fpc));
  }

  action request_uri { 
    if(parser->request_uri != NULL)
      parser->request_uri(parser->data, PTR_TO(mark), LEN(mark, fpc));
  }

  action fragment {
    if(parser->fragment != NULL)
      parser->fragment(parser->data, PTR_TO(mark), LEN(mark, fpc));
  }

  action start_query {MARK(query_start, fpc); }
  action query_string { 
    if(parser->query_string != NULL)
      parser->query_string(parser->data, PTR_TO(query_start), LEN(query_start, fpc));
  }

  action http_version {	
    if(parser->http_version != NULL)
      parser->http_version(parser->data, PTR_TO(mark), LEN(mark, fpc));
  }

  action request_path {
    if(parser->request_path != NULL)
      parser->request_path(parser->data, PTR_TO(mark), LEN(mark,fpc));
  }

  action done {
      if(parser->xml_sent || parser->json_sent) {
        parser->body_start = PTR_TO(mark) - buffer;
        // +1 includes the \0
        parser->content_len = fpc - buffer - parser->body_start + 1;
      } else {
        parser->body_start = fpc - buffer + 1;

        if(parser->header_done != NULL) {
          parser->header_done(parser->data, fpc + 1, pe - fpc - 1);
        }
      }
    fbreak;
  }

  action xml {
      parser->xml_sent = 1;
  }

  action json {
      parser->json_sent = 1;
  }


#### HTTP PROTOCOL GRAMMAR
  CRLF = ( "\r\n" | "\n" ) ;

  # URI description as per RFC 3986.

  more_delims   = ( "{" | "}" | "^" ) when { parser->uri_relaxed } ;
  sub_delims    = ( "!" | "$" | "&" | "'" | "(" | ")" | "*"
                  | "+" | "," | ";" | "=" | more_delims ) ;
  gen_delims    = ( ":" | "/" | "?" | "#" | "[" | "]" | "@" ) ;
  reserved      = ( gen_delims | sub_delims ) ;
  unreserved    = ( alpha | digit | "-" | "." | "_" | "~" ) ;

  pct_encoded   = ( "%" xdigit xdigit ) ;

# pchar         = ( unreserved | pct_encoded | sub_delims | ":" | "@" ) ;
# add (any -- ascii) support chinese
  pchar         = ( (any -- ascii) | unreserved | pct_encoded | sub_delims | ":" | "@" ) ;

  fragment      = ( ( pchar | "/" | "?" )* ) >mark %fragment ;

  query         = ( ( pchar | "/" | "?" )* ) %query_string ;

  # non_zero_length segment without any colon ":" ) ;
  segment_nz_nc = ( ( unreserved | pct_encoded | sub_delims | "@" )+ ) ;
  segment_nz    = ( pchar+ ) ;
  segment       = ( pchar* ) ;

  path_empty    = ( pchar{0} ) ;
  path_rootless = ( segment_nz ( "/" segment )* ) ;
  path_noscheme = ( segment_nz_nc ( "/" segment )* ) ;
  path_absolute = ( "/" ( segment_nz ( "/" segment )* )? ) ;
  path_abempty  = ( ( "/" segment )* ) ;

  path          = ( path_abempty    # begins with "/" or is empty
                  | path_absolute   # begins with "/" but not "//"
                  | path_noscheme   # begins with a non-colon segment
                  | path_rootless   # begins with a segment
                  | path_empty      # zero characters
                  ) ;

  reg_name      = ( unreserved | pct_encoded | sub_delims )* ;

  dec_octet     = ( digit                 # 0-9
                  | ("1"-"9") digit         # 10-99
                  | "1" digit{2}          # 100-199
                  | "2" ("0"-"4") digit # 200-249
                  | "25" ("0"-"5")      # 250-255
                  ) ;

  IPv4address   = ( dec_octet "." dec_octet "." dec_octet "." dec_octet ) ;
  h16           = ( xdigit{1,4} ) ;
  ls32          = ( ( h16 ":" h16 ) | IPv4address ) ;

  IPv6address   = (                               6( h16 ":" ) ls32
                  |                          "::" 5( h16 ":" ) ls32
                  | (                 h16 )? "::" 4( h16 ":" ) ls32
                  | ( ( h16 ":" ){1,} h16 )? "::" 3( h16 ":" ) ls32
                  | ( ( h16 ":" ){2,} h16 )? "::" 2( h16 ":" ) ls32
                  | ( ( h16 ":" ){3,} h16 )? "::"    h16 ":"   ls32
                  | ( ( h16 ":" ){4,} h16 )? "::"              ls32
                  | ( ( h16 ":" ){5,} h16 )? "::"              h16
                  | ( ( h16 ":" ){6,} h16 )? "::"
                  ) ;

  IPvFuture     = ( "v" xdigit+ "." ( unreserved | sub_delims | ":" )+ ) ;

  IP_literal    = ( "[" ( IPv6address | IPvFuture  ) "]" ) ;

  port          = ( digit* ) ;
  host          = ( IP_literal | IPv4address | reg_name ) ;
  userinfo      = ( ( unreserved | pct_encoded | sub_delims | ":" )* ) ;
  authority     = ( ( userinfo "@" )? host ( ":" port )? ) ;

  scheme        = ( alpha ( alpha | digit | "+" | "-" | "." )* ) ;

  relative_part = ( "//" authority path_abempty
                  | path_absolute
                  | path_noscheme
                  | path_empty
                  ) ;


  hier_part     = ( "//" authority path_abempty
                  | path_absolute
                  | path_rootless
                  | path_empty
                  ) ;

  absolute_URI  = ( scheme ":" hier_part ( "?" query )? ) ;

  relative_ref  = ( (relative_part %request_path ( "?" %start_query query )?) >mark %request_uri ( "#" fragment )? ) ;
  URI           = ( scheme ":" (hier_part  %request_path ( "?" %start_query query )?) >mark %request_uri ( "#" fragment )? ) ;

  URI_reference = ( URI | relative_ref ) ;

# HTTP header parsing
  Method = ( upper | digit ){1,20} >mark %request_method;

  http_number = ( "1." ("0" | "1") ) ;
  HTTP_Version = ( "HTTP/" http_number ) >mark %http_version ;
  Req_Line = ( Method " " URI_reference " " HTTP_Version CRLF ) ;

  HTTP_CTL = (0 - 31) | 127 ;
  HTTP_separator = ( "(" | ")" | "<" | ">" | "@"
                   | "," | ";" | ":" | "\\" | "\""
                   | "/" | "[" | "]" | "?" | "="
                   | "{" | "}" | " " | "\t"
                   ) ;

  lws = CRLF? (" " | "\t")+ ;
  token = ascii -- ( HTTP_CTL | HTTP_separator ) ;
  content = ((any -- HTTP_CTL) | lws);

  field_name = ( token )+ >start_field %write_field;

  field_value = content* >start_value %write_value;

  message_header = field_name ":" lws* field_value :> CRLF;

  Req = Req_Line ( message_header )* ( CRLF );

  SockJSONStart = ("@" relative_part);
  SockJSONData = "{" any* "}" :>> "\0";

  SockXMLData = ("<" [a-z0-9A-Z\-.]+) >mark %request_path ("/" | space | ">") any* ">" :>> "\0";

  SockJSON = SockJSONStart >mark %request_path " " SockJSONData >mark @json;
  SockXML = SockXMLData @xml;

  SockReq = (SockXML | SockJSON);

main := (Req | SockReq) @done;

}%%

/** Data **/
%% write data;

int http_parser_init(http_parser *parser) {
  int cs = 0;
  %% write init;
  parser->cs = cs;
  parser->body_start = 0;
  parser->content_len = 0;
  parser->mark = 0;
  parser->nread = 0;
  parser->field_len = 0;
  parser->field_start = 0;
  parser->xml_sent = 0;
  parser->json_sent = 0;

  return(1);
}


/** exec **/
size_t http_parser_execute(http_parser *parser, const char *buffer, size_t len, size_t off)  
{
  if(len == 0) return 0;
  parser->nread = 0;
  parser->mark = 0;
  parser->field_len = 0;
  parser->field_start = 0;
 
  const char *p, *pe;
  int cs = parser->cs;

  assert(off <= len && "offset past end of buffer");
  //函数使用指针 p 和 pe 来指示待解析的数据块的起始和结束位置。这些指针根据传入的偏移量 off 和长度 len 计算而来。
  p = buffer+off;
  pe = buffer+len;

  assert(pe - p == (int)len - (int)off && "pointers aren't same distance");
  //函数通过 %% write exec; 执行解析器的主要逻辑
  /**
  在 Ragel 中，`%% write exec` 表示在状态机的定义中，可以编写自定义的代码块来执行特定的逻辑。这是 Ragel 提供的一种机制，允许您在状态机的不同状态下插入自己的代码，以便在解析过程中执行特定的操作。
  具体来说，`%% write exec` 表示在状态机的当前状态下，执行一个自定义的代码块，该代码块会根据状态机的输入和状态执行相应的逻辑。这通常用于在解析过程中执行一些特殊的操作，例如记录日志、更新解析器状态、调用回调函数等。
  `exec` 后面的代码块可以包含任何合法的编程语言代码，具体取决于您的需求和程序的实现。在您的示例中，`%% write exec;` 表示在状态机的当前状态下执行自定义的代码块，但具体的代码块内容并未提供，它会根据 Ragel 的状态机定义和上下文来确定。
  通常，`%% write exec` 部分是为了在状态机的特定状态下执行与解析相关的操作。在 HTTP 请求解析器中，这些操作可能包括记录日志、构建请求对象、调用回调函数等，以根据解析的结果进行后续处理。
  **/
  %% write exec;

  assert(p <= pe && "Buffer overflow after parsing.");

  if (!http_parser_has_error(parser)) {
      parser->cs = cs;
  }

  parser->nread += p - (buffer + off);

  assert(parser->nread <= len && "nread longer than length");
  assert(parser->body_start <= len && "body starts after buffer end");
  assert(parser->mark < len && "mark is after buffer end");
  assert(parser->field_len <= len && "field has length longer than whole buffer");
  assert(parser->field_start < len && "field starts after buffer end");

  return(parser->nread);
}

int http_parser_finish(http_parser *parser)
{
  if (http_parser_has_error(parser) ) {
    return -1;
  } else if (http_parser_is_finished(parser) ) {
    return 1;
  } else {
    return 0;
  }
}

int http_parser_has_error(http_parser *parser) {
  return parser->cs == http_parser_error;
}

int http_parser_is_finished(http_parser *parser) {
  return parser->cs >= http_parser_first_final;
}
