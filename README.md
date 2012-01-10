
# Module for Apache 2.x which automates serving static content with NGINX local proxy
<br>

## License and copyrights

`mod_aclr2` is the port of the original `mod_aclr` module for Apache 1.3
written by [Dmitry MikSir](http://miksir.maker.ru).<br>
Original code and documentation are still available
[here](http://miksir.maker.ru/?r=72).<br>
Copyright (C) Dmitry MikSir

This module, in contrast to `mod_aclr`, works only under Apache 2.x.

Original `mod_aclr` was initially released under GNU GPL license,
but its author granted the permission to release new `mod_aclr2`
under BSD license.

Copyright (C) 2011-2012 Andrey Belov<br>
Copyright (C) 2011-2012 Nginx, Inc., [http://nginx.com](http://nginx.com)

## Introduction

`mod_aclr2` ("aclr" comes from <i>**ac**ce**l r**edirect</i>) is the module for
Apache 2.x, that makes it possible to easily deploy [NGINX](http://nginx.com)
as a local reverse proxy in front of [Apache](http://httpd.apache.org).

As a result, all your static content can now be automatically served
by NGINX, while the other legacy and Apache-specific things like
RewriteRules, .htaccesses, embedded PHP/Perl, CGIs will work as they worked
before. This setup can be very important and useful for many shared hosting
service providers to improve the situation with the concurrency and quality
of service.

Efficiency in processing any kind of static content is just one of the
[NGINX benefits](http://nginx.com/papers/nginx-features.pdf),
therefore mod_aclr2 is a good starting point for further improvements
to your existing hosting infrastructure - you could do many things
better and faster with NGINX.

## How it works?

The module creates a special Apache handler hook. This handler
must be processed after all the other registered handlers,
and before generating any response to the client
(NGINX, in our case).

First, it checks for the `X-Accel-Internal` header presence.
If this header is present, and if the result of the request is a regular file,
the module sends a special header
[`X-Accel-Redirect`](http://wiki.nginx.org/X-accel#X-Accel-Redirect)
back to NGINX. This header contains the value of `X-Accel-Internal`
header combined with the exact location of the file
as it was determined by Apache after processing all previous
hooks (mod_rewrite, .htaccess, etc).

All requests with dynamic responses (except SSI) should never
reach `mod_aclr2` handler. Every SSI request is redirected to the core
directly from `mod_aclr2` handler if
[INCLUDES](http://httpd.apache.org/docs/2.2/mod/mod_include.html) filter
was found in the output filters chain.

The fact that the presence of the `X-Accel-Internal` is required
allows you to control precisely when to make NGINX serve the static
file or go directly to the backend. Please note that in order for NGINX
to serve the static content directly, it should run as a local proxy,
on the same machine as Apache and having the same access to the files on disk.

## Configuration directives

 syntax: **AccelRedirectSet** On | Off<br>
 context: server config, virtual host, directory<br>
 default: Off<br>

&nbsp;&nbsp;&nbsp; Enables or disables the module.

 syntax: **AccelRedirectSize** size[b|k|m]<br>
 context: server config, virtual host, directory<br>
 default: -1<br>
 
&nbsp;&nbsp;&nbsp; Sets the minimum size of static files for which the
 request will be handled by NGINX.<br>
&nbsp;&nbsp;&nbsp; By default, there is no any restrictions, e.g.
Apache will send redirects to NGINX for all static files.

 syntax: **AccelRedirectOutsideDocRoot** On | Off<br>
 context: server config, virtual host, directory<br>
 default: Off<br>

&nbsp;&nbsp;&nbsp; Enables or disables redirects for files
outside of DocumentRoot path.

 syntax: **AccelRedirectDebug** 0-4<br>
 context: server config<br>
 default: 0<br>

&nbsp;&nbsp;&nbsp; <b>WARNING: module must be built with -DDEBUG to enable this directive.</b><br>
&nbsp;&nbsp;&nbsp; Sets the debug level:<br>

 *	0 = no debug
 *	1 = log successfull redirects only
 *	2 = also, log a reason why request has not been redirected
 *	3 = also, log a lot of request processing data

## Apache: mod_aclr2 installation and configuration

 1. Compile the module using [`apxs`](http://man.cx/apxs).

        apxs -c mod_aclr2.c

 2. Install and activate it.

        apxs -i -a -n mod_aclr2 mod_aclr2.la

 3. Configure the module in the Apache configuration file `httpd.conf`
    or equivalent. For example:
 
        AccelRedirectSet On
        AccelRedirectSize 1k

 4. Reload Apache.
 
## NGINX configuration

The configuration relies on the fact that `mod_aclr2` **requires** the
header `X-Accel-Internal` to be present.

 1. Proxy configuration:

        location / {
            proxy_pass http://127.0.0.1:80;
            proxy_set_header X-Accel-Internal /int;
            proxy_set_header Host $host;
        }

     The location `/int` is the one that will handle the
     static file serving by NGINX.

 2. Special internal location:
  
        location /int/ {
            # root must be equal to Apache's DocumentRoot
            root /var/www/site.com;
            rewrite ^/int/(.*)$ /$1 break;
            internal;
         }

According to this configuration, request from NGINX to Apache should look like:

        GET /some/static/file.txt HTTP/1.1
        Host: site.com
        X-Accel-Internal: /int

and response from `mod_aclr2` to NGINX should look like:

        HTTP/1.1 200 OK
        Date: Tue, 10 Jan 2012 02:59:02 GMT
        Server: Apache/2.2.21 (FreeBSD)
        X-Accel-Version: 0.01
        X-Accel-Redirect: /int/some/static/file.txt
        Content-Type: text/html

and, finally, NGINX sends the file `/var/www/site.com/some/static/file.txt`
back to the client.

## Bugs

Probably exist. Feel free to report. Patches are welcome.

