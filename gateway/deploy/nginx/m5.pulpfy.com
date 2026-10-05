# /etc/nginx/sites-available/m5.pulpfy.com
# Stack-Chan gateway: container stackchan-gateway, published on 127.0.0.1:3150 only.
# certbot --nginx adds the 443 listener and the http->https redirect to this file.
server {
    listen 80;
    listen [::]:80;
    server_name m5.pulpfy.com;

    server_tokens off;
    access_log /var/log/nginx/m5.pulpfy.com.access.log m5_noquery;
    error_log /var/log/nginx/m5.pulpfy.com.error.log;
    client_max_body_size 1m;

    location / {
        limit_req zone=m5_gateway burst=20 nodelay;
        limit_except GET HEAD { deny all; }
        proxy_pass http://127.0.0.1:3150;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_read_timeout 60s;
        proxy_buffering off; # audio streaming (phase 6)
    }
}
