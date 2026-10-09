# /etc/nginx/sites-available/m5.pulpfy.com
# Stack-Chan gateway panel + robot audio: container stackchan-gateway, published on 127.0.0.1:3150 only.
# Installed by gateway/scripts/update-nginx.sh (backup + nginx -t + automatic restore on failure).
server {
    server_name m5.pulpfy.com;

    server_tokens off;
    access_log /var/log/nginx/m5.pulpfy.com.access.log m5_noquery;
    error_log /var/log/nginx/m5.pulpfy.com.error.log;
    client_max_body_size 1m;
    limit_req_status 429;
    add_header Strict-Transport-Security "max-age=31536000" always;

    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
    proxy_read_timeout 60s;

    # Password guessing: 10 per minute per address at the edge (the gateway also locks after 5 failures)
    location = /api/login {
        limit_req zone=m5_login burst=5 nodelay;
        proxy_pass http://127.0.0.1:3150;
    }

    # Music uploads: streamed to the gateway, which converts them for the robot
    location = /api/music/upload {
        limit_req zone=m5_gateway burst=5 nodelay;
        client_max_body_size 64m;
        proxy_request_buffering off;
        proxy_read_timeout 180s;
        proxy_pass http://127.0.0.1:3150;
    }

    # Live activity feed (server-sent events)
    location = /api/events {
        proxy_buffering off;
        proxy_read_timeout 1h;
        proxy_pass http://127.0.0.1:3150;
    }

    location / {
        limit_req zone=m5_gateway burst=40 nodelay;
        limit_except GET HEAD POST PUT PATCH DELETE { deny all; }
        proxy_buffering off; # robot audio downloads
        proxy_pass http://127.0.0.1:3150;
    }

    listen 443 ssl; # managed by Certbot
    listen [::]:443 ssl; # managed by Certbot
    ssl_certificate /etc/letsencrypt/live/m5.pulpfy.com/fullchain.pem; # managed by Certbot
    ssl_certificate_key /etc/letsencrypt/live/m5.pulpfy.com/privkey.pem; # managed by Certbot
    include /etc/letsencrypt/options-ssl-nginx.conf; # managed by Certbot
    ssl_dhparam /etc/letsencrypt/ssl-dhparams.pem; # managed by Certbot
}

server {
    if ($host = m5.pulpfy.com) {
        return 301 https://$host$request_uri;
    } # managed by Certbot

    listen 80;
    listen [::]:80;
    server_name m5.pulpfy.com;
    return 404; # managed by Certbot
}
