#include "httpclient.h"

#include <stdlib.h>
#include <stdio.h>
//#include <zlib.h>

#include <errno.h>

#include <stdlib.h>

#ifdef WIN32
#include <WinSock2.h>
#pragma comment( lib, "ws2_32.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#endif // WIN32


#define BUFFER_CHUNK_SIZE 10240
#define HTTP_PORT         80
#define HTTPS_PORT        443 /* no https support for now */
#define HTTP_1_1_STR      "HTTP/1.1"

static const char* HTTP_REQUESTS[] =
{
    "GET",
    "HEAD",
    "POST",
    "PUT",
    "DELETE",
    "TRACE",
    "OPTIONS",
    "CONNECT",
    "PATCH"
};

static char* URL_encode(char* const addr)
{
    uint16_t special_chars_count = 0;
    static const char reserved_chars[] = "!*\'();:@&=+$,/=#[]-_.~?";

    /* do one round to find the size */
    for (char* c = &addr[0]; *c != '\0'; ++c)
    {
        if (!isalpha(*c) && !isdigit(*c) && strchr(reserved_chars, *c) == NULL)
            ++special_chars_count;
    }

    char* enc_addr = (char*) malloc(strlen(addr) + 2 * special_chars_count + 1);
    char* new = &enc_addr[0];
    for (char* old = &addr[0]; *old != '\0';)
    {
        if (!isalpha(*old) && !isdigit(*old) && strchr(reserved_chars, *old) == NULL)
        {
            new += sprintf(new, "%%%X", *old++);
        }
        else
            *new++ = *old++;
    }

    *new = '\0';

    return enc_addr;
}

static void word_to_string(const char* const word, char** str)
{
    uint32_t i = 0;
    while (word[i] == ' ' || word[i] == '\r' || word[i] == '\n' || word[i] == '\0')
        ++i;

    uint32_t start = i;

    while (word[i] != ' ' && word[i] != '\r' && word[i] != '\n')
        ++i;

    *str = (char*) malloc(i - start + 1);
    (*str)[i - start] = '\0';
    memcpy(*str, &word[start], i - start);
}

static bool dissect_address(char* const address, char* host, const size_t max_host_length, char* resource, const size_t max_resource_length)
{
    char* encoded_addr = URL_encode(address);

    /* remove any protocol headers (f.e. "http://") before we search for first '/' */
    char* start_pos = strstr(encoded_addr, "://");
    start_pos = ((start_pos == NULL) ? (char*) encoded_addr : start_pos + 3);

    char* slash_pos = strchr(start_pos, '/');

    /* no resource, send request to index.html*/
    if (slash_pos == NULL || slash_pos[1] == '\0')
    {
        strcpy(host, start_pos);

        /* remove slash from host if any */
        if (slash_pos != NULL)
            strchr(host, '/')[0] = '\0';

        //strcpy(resource, "/index.html");
        strcpy(resource, "/");
        free(encoded_addr);
        return true;
    }

    char* addr_end = strchr(slash_pos, '\0');

    if (start_pos >= slash_pos - 1)
    {
        free(encoded_addr);
        return false;
    }

    if (slash_pos - start_pos > max_host_length)
    {
        free(encoded_addr);
        return false;
    }

    if (addr_end - slash_pos > max_resource_length)
    {
        free(encoded_addr);
        return false;
    }

    strcpy(host, start_pos);
    host[slash_pos - start_pos] = '\0';
    strcpy(resource, slash_pos);
    resource[addr_end - slash_pos] = '\0';

    free(encoded_addr);
    return true;
}

static void print_status(http_ret_t status)
{
    switch (status)
    {
    case HTTP_SUCCESS:
        printf("SUCCESS\n");
        return;
    case HTTP_ERR_OPENING_SOCKET:
        printf("ERROR: OPENING SOCKET\n");
        break;
    case HTTP_ERR_DISSECT_ADDR:
        printf("ERROR: DISSECTING ADDRESS\n");
        break;
    case HTTP_ERR_NO_SUCH_HOST:
        printf("ERROR: NO SUCH HOST\n");
        break;
    case HTTP_ERR_CONNECTING:
        printf("ERROR: CONNECTING TO HOST\n");
        break;
    case HTTP_ERR_WRITING:
        printf("ERROR: WRITING TO SOCKET\n");
        break;
    case HTTP_ERR_READING:
        printf("ERROR: READING FROM SOCKET\n");
        break;
    default:
        printf("ERROR: UNKNOWN STATUS CODE\n");
    }
    printf("errno: %s\n", strerror(errno));
}

int ft_http_init()
{
#ifdef WIN32

    WORD    version = MAKEWORD(2, 2);
    WSADATA data;
    WSAStartup(version, &data);
#endif

    return 0;
}

static bool build_http_request(const char* host, const char* resource, const http_req_t http_req, char* request,
                               const size_t max_req_size, char** header_lines, const size_t header_line_count, char* const body)
{
    //char* accept_encoding = "Accept-Encoding: gzip\r\n";
    char* accept_encoding = "";
    sprintf(request, "%s %s %s\r\nHost: %s\r\n%sReferer: http://%s%s\r\n",
            HTTP_REQUESTS[http_req],
            resource,
            HTTP_1_1_STR,
            host,
            accept_encoding,
            host,
            resource);
    for (uint16_t i = 0; i < header_line_count; ++i)
    {
        sprintf(request, "%s%s\r\n", request, header_lines[i]);
    }
    strcat(request, "\r\n");
    if (body != NULL)
        strcat(request, body);

    return true;
}

static bool http_body_get(const char* http_resp, char* body, size_t max_body_length)
{

    uint32_t i, j = 0;
    char matchseq[] = {'\r', '\n', '\r', '\n'};

    for (i = 0; i < 80000; ++i)
    {
        if (http_resp[i] == matchseq[j])
        {
            ++j;
            if (j == sizeof(matchseq))
            {
                body = (char*) &http_resp[i];
                return true;
            }
        }
        else
        {
            j = 0;
        }
    }
    return false;
}

static void print(uint8_t* msg, uint32_t len)
{
    printf("-----------------\n");
    uint32_t i = 0;
    for (; i < len; ++i)
        printf("%2X ", msg[i]);
    printf("\n");
}

static void socket_set_timeout(socket_t socket, uint32_t seconds, uint32_t usec)
{
    struct timeval tv;

    tv.tv_sec = seconds;
    tv.tv_usec = usec;

    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));
}

static socket_t socket_open(struct hostent* host, int portno)
{
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0)
        return -1;

    struct sockaddr_in server_addr;

    memset((uint8_t*) &server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;

    memcpy((uint8_t*) &server_addr.sin_addr.s_addr, (uint8_t*) host->h_addr, host->h_length);

    server_addr.sin_port = htons(portno);

    /* create TCP connection to host */
    int res =  connect(sock, (struct sockaddr*) &server_addr, sizeof(server_addr));
    if (res < 0)
    {
        return -1;
    }

    return sock;
}

static void socket_close(socket_t socket)
{
    if (socket >= 0)
        close(socket);
}

static void free_header(http_header_t* p_header)
{
    if (p_header->content_type != NULL)
        free(p_header->content_type);
    if (p_header->encoding != NULL)
        free(p_header->encoding);
    if (p_header->status_text != NULL)
        free(p_header->status_text);
    if (p_header->redirect_addr != NULL)
        free(p_header->redirect_addr);

    free(p_header);
}

static void dissect_header(char* data, http_response_t* p_resp)
{
    if (strlen(data) == 0)
    {
        p_resp->p_header = NULL;
        return;
    }
    p_resp->p_header = (http_header_t*) malloc(sizeof(http_header_t));

    char* content = data;
    char* header_end = strstr(content, "\r\n\r\n");
    if (header_end == NULL)
    {
        header_end = content + strlen(content);
    }
    else
    {
        /* explicitly null terminate header, to satisfy strstr() */
        header_end[0] = '\0';
    }

    /* handle first line. FORMAT:[HTTP/1.1 <STATUS_CODE> <STATUS_TEXT>] */
    content = strchr(data, ' ');
    if (content == NULL)
        return;

    p_resp->p_header->status_code = atoi(content);
    content = strchr(content + 1, ' ') + 1;

    size_t status_text_len = strchr(content, '\r') - content;

    p_resp->p_header->status_text = (char*) malloc(status_text_len);
    memcpy(p_resp->p_header->status_text, content, status_text_len);

    p_resp->p_header->content_type = NULL;
    p_resp->p_header->encoding = NULL;
    p_resp->p_header->redirect_addr = NULL;

    while (content <= header_end)
    {
        if (content == NULL)
            return;
        content = strstr(content, "\r\n");
        if (content == NULL)
            return;
        content += 2;

        if (strstr(content, "Content-Type:") == content)
        {
            content = strchr(content, ' ');
            word_to_string(content, &p_resp->p_header->content_type);
        }
        else if (strstr(content, "Content-Encoding:") == content)
        {
            content = strchr(content, ' ');
            word_to_string(content, &p_resp->p_header->encoding);
        }
        else if (strstr(content, "Location:") == content || strstr(content, "location:") == content)
        {
            content = strchr(content, ' ');
            word_to_string(content, &p_resp->p_header->redirect_addr);
        }
    }
}

/**
* performs the actual http request.
*/
static char* _http_request(char* const address, http_req_t http_req, http_ret_t* p_ret,
                           uint32_t* resp_len, char** header_lines, size_t header_line_count, char* const body)
{
    int portno = HTTP_PORT;

    if (strstr(address, "https://") != NULL)
    {
        *p_ret = HTTP_ERR_IS_HTTPS;
        return NULL;
    }

    struct hostent* server;

    char host_addr[256];
    char resource_addr[256];

    if (!dissect_address(address, host_addr, 256, resource_addr, 256))
    {
        *p_ret = HTTP_ERR_DISSECT_ADDR;
        return NULL;
    }

    /* do DNS lookup */
    server = gethostbyname(host_addr);
    int errcode = WSAGetLastError();

    if (server == NULL)
    {
        *p_ret = HTTP_ERR_NO_SUCH_HOST;
        return NULL;
    }

    /* open socket to host */
    socket_t sock = socket_open(server, portno);

    if (sock < 0)
    {
        *p_ret = HTTP_ERR_OPENING_SOCKET;
        return NULL;
    }

    /* Default timeout is too long */
    socket_set_timeout(sock, 1, 0);

    size_t http_req_size = 256 + strlen(address);
    for (size_t i = 0; i < header_line_count; ++i)
    {
        http_req_size += strlen(header_lines[i]);
    }

    if (body != NULL)
        http_req_size += strlen(body);

    char* http_req_str = (char*) malloc(http_req_size);

    build_http_request(host_addr, resource_addr, http_req, http_req_str, http_req_size, header_lines, header_line_count, body);

    /* send http request */
    int len = send(sock, http_req_str, strlen(http_req_str), 0);   // change write to send

    free(http_req_str);

    if (len < 0)
    {
        *p_ret = HTTP_ERR_WRITING;
        return NULL;
    }

    uint32_t buffer_len = BUFFER_CHUNK_SIZE;
    uint8_t* resp_str = (uint8_t*) malloc(buffer_len);
    memset(resp_str, 0, buffer_len);
    int32_t tot_len = 0;
    uint32_t cycles = 0;

    /* Read response from host */
    do
    {
        len = recv(sock, &resp_str[tot_len], buffer_len - tot_len, 0);

        if (len <= 0 && cycles >= 0)
            break;

        tot_len += len;

        if (tot_len >= buffer_len)
        {
            buffer_len += BUFFER_CHUNK_SIZE;
            uint8_t* newbuf = (uint8_t*) realloc(resp_str, buffer_len);

            if (newbuf == NULL)
            {
                *p_ret = HTTP_ERR_OUT_OF_MEM;
                free(resp_str);
                return NULL;
            }

            resp_str = newbuf;
            memset(&resp_str[buffer_len - BUFFER_CHUNK_SIZE], 0, BUFFER_CHUNK_SIZE);
        }

        ++cycles;

    }
    while (true);

    if (tot_len <= 0)
    {
        *p_ret = HTTP_ERR_READING;
        free(resp_str);
        return NULL;
    }
    /* shave buffer */
    uint8_t* new_str = (uint8_t*) realloc(resp_str, tot_len);

    if (new_str == NULL)
    {
        *p_ret = HTTP_ERR_OUT_OF_MEM;
        free(resp_str);
        return NULL;
    }
    resp_str = new_str;

    *resp_len = (tot_len);

    socket_close(sock);

    return resp_str;
}

http_response_t* http_request_w_body(char* const address, const http_req_t http_req, char** header_lines, size_t header_line_count, char* const body)
{
    http_response_t* p_resp = (http_response_t*) malloc(sizeof(http_response_t));
    memset(p_resp, 0, sizeof(http_response_t));

    uint32_t tot_len, header_len, body_len;
    uint8_t *body_pos, *resp_str;

    char* address_copy = address;

    /* loop until a non-redirect page is found (up to 5 times, as per spec recommendation) */
    for (uint8_t redirects = 0; redirects < 8; ++redirects)
    {
        resp_str = _http_request(address_copy, http_req, &p_resp->status, &tot_len, header_lines, header_line_count, body);

        if (resp_str == NULL)
        {
            return p_resp;
        }

        /* header ends with an empty line */
        body_pos = strstr(resp_str, "\r\n\r\n") + 4;
        header_len = (body_pos - resp_str);
        body_len = (tot_len - header_len);

        /* place data in response header */
        dissect_header(resp_str, p_resp);

        if (p_resp->p_header == NULL)
        {
            p_resp->status = HTTP_ERR_BAD_HEADER;
            return p_resp;
        }

        if (p_resp->p_header->status_code >= 300 && p_resp->p_header->status_code < 400)
        {
            if (p_resp->p_header->redirect_addr == NULL)
            {
                p_resp->status = HTTP_ERR_BAD_HEADER;
                return p_resp;
            }

            if (address_copy != address)
                free(address_copy);

            address_copy = (char*) malloc(strlen(p_resp->p_header->redirect_addr));
            strcpy(address_copy, p_resp->p_header->redirect_addr);

            free_header(p_resp->p_header);
            p_resp->p_header = NULL;

            free(resp_str);
            resp_str = NULL;
        }
        else
        {
            break;
        }
    }

    if (p_resp->p_header == NULL)
    {
        p_resp->status = HTTP_ERR_TOO_MANY_REDIRECTS;
        return p_resp;
    }


    /* if contents are compressed, uncompress it before placing it in the struct */
    if (p_resp->p_header->encoding != NULL && strstr(p_resp->p_header->encoding, "gzip") != NULL)
    {
        /* content length is always stored in the 4 last bytes */
        uint32_t content_len = 0;
        memcpy(&content_len, resp_str + tot_len - 4, 4);

        /* safe guard against insane sizes */
        content_len = ((content_len < 30 * body_len) ? content_len : 30 * body_len);

        p_resp->contents = (char*) malloc(content_len);

        if (p_resp->contents == NULL)
        {
            p_resp->status = HTTP_ERR_OUT_OF_MEM;
            return p_resp;
        }

        /* zlib decompression (gzip) */
        /*    z_stream infstream;
            memset(&infstream, 0, sizeof(z_stream));
            infstream.avail_in = (uInt)(body_len);
            infstream.next_in = (Bytef *)body_pos;
            infstream.avail_out = (uInt)content_len;
            infstream.next_out = (Bytef *)p_resp->contents;

            inflateInit2(&infstream, 16 + MAX_WBITS);
            inflate(&infstream, Z_NO_FLUSH);
            inflateEnd(&infstream);*/  //anzx

        p_resp->length = content_len;
    }
    else if (body_len > 0)
    {
        p_resp->contents = (char*) malloc(body_len + 1);
        p_resp->contents[body_len] = '\0';
        memcpy(p_resp->contents, body_pos, body_len);
        p_resp->length = body_len;
    }
    else
    {
        /* response without body */
        free(resp_str);
        p_resp->contents = NULL;
        p_resp->status = HTTP_EMPTY_BODY;
        return p_resp;
    }

    free(resp_str);
    return p_resp;
}


http_response_t* http_request(char* const address, const http_req_t http_req, char** header_lines, size_t header_line_count)
{
    return http_request_w_body(address, http_req, header_lines, header_line_count, NULL);
}


void http_response_free(http_response_t* p_http_resp)
{
    if (p_http_resp == NULL)
        return;
    free_header(p_http_resp->p_header);
    if (p_http_resp->contents != NULL)
        free(p_http_resp->contents);
    free(p_http_resp);
}


