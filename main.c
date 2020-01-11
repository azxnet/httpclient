#include "httpclient.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


static void print_status(http_ret_t status)
{
    switch (status)
    {
    case HTTP_SUCCESS:
        printf("SUCCESS\n");
        return;
    case HTTP_EMPTY_BODY:
        printf("EMPTY BODY\n");
        return;
    case HTTP_ERR_OPENING_SOCKET:
        printf("ERROR: OPENING SOCKET\n");
        return;
    case HTTP_ERR_DISSECT_ADDR:
        printf("ERROR: DISSECTING ADDRESS\n");
        return;
    case HTTP_ERR_NO_SUCH_HOST:
        printf("ERROR: NO SUCH HOST\n");
        return;
    case HTTP_ERR_CONNECTING:
        printf("ERROR: CONNECTING TO HOST\n");
        return;
    case HTTP_ERR_WRITING:
        printf("ERROR: WRITING TO SOCKET\n");
        return;
    case HTTP_ERR_READING:
        printf("ERROR: READING FROM SOCKET\n");
        return;
    case HTTP_ERR_OUT_OF_MEM:
        printf("ERROR: OUT OF MEMORY\n");
        return;
    case HTTP_ERR_BAD_HEADER:
        printf("ERROR: BAD HEADER\n");
        return;
    case HTTP_ERR_TOO_MANY_REDIRECTS:
        printf("ERROR: TOO MANY REDIRECTS\n");
        return;
    case HTTP_ERR_IS_HTTPS:
        printf("ERROR: ADDRESS IS HTTPS, NOT SUPPORTED.\n");
        return;
    default:
        printf("ERROR: UNKNOWN STATUS CODE\n");
    }
}

static void safe_free(void** ptr)
{
    if (ptr != NULL)
        free(*ptr);
    *ptr = NULL;
}


int main(int argc, char* args[])
{
    /*if (argc != 2)
    {
      printf("Usage: ./httpclient <web-address>\n");
      return 64;
    }*/

    ft_http_init();
    char* addr = NULL;
    //addr = args[1];
    addr = "http://www.baidu.com/";
    http_response_t* p_resp = http_request(addr, HTTP_REQ_GET, NULL, 0);
    if (p_resp->status == HTTP_SUCCESS)
    {
        /* received file. If it is an image, store it.*/
        if (p_resp->p_header->content_type != NULL && strstr(p_resp->p_header->content_type, "image") != NULL)
        {
            /* find last '/' in address to get the image name */
            char* img = &addr[0];
            char* temp = &addr[0];
            do
            {
                temp = strchr(img, '/');
                if (temp == NULL)
                    break;
                img = temp + 1;
            }
            while(true);

            /* store as local file */
            FILE* output = fopen(img, "wb");
            if (output != NULL)
            {
                fwrite(p_resp->contents, 1, p_resp->length, output);
                fclose(output);
            }
            printf("Stored image as %s\n", img);
        }
        else
        {
            /* if not an image, print it. */
            printf("RESPONSE FROM %s:\n%s\n", addr, p_resp->contents);
        }
    }
    else if (p_resp->status == HTTP_EMPTY_BODY)
    {
        printf("RESPONSE FROM %s had no content. Reason: %s (%i)\n", addr, p_resp->p_header->status_text, p_resp->p_header->status_code);
    }
    else
    {
        print_status(p_resp->status);
    }

    if (p_resp->p_header != NULL)
    {
        safe_free((void**)&p_resp->p_header->status_text);
        safe_free((void**)&p_resp->p_header->content_type);
        safe_free((void**)&p_resp->p_header->encoding);
        safe_free((void**)&p_resp->p_header->redirect_addr);
    }
    safe_free((void**)&p_resp->p_header);
    safe_free((void**)&p_resp->contents);
    safe_free((void**)&p_resp);
    return 0;
}
