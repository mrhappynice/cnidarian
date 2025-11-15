# Dockerfile (serving image)
FROM debian:stable-slim

RUN apt-get update && apt-get install -y nginx \
 && rm -rf /var/lib/apt/lists/*

# Remove the default site so we don't have duplicate default servers
RUN rm -f /etc/nginx/sites-enabled/default

# Web root
RUN mkdir -p /var/www/wasm-c-demo

# Copy frontend and built wasm/js
# Expecting on host:
#   ./web/index.html
#   ./web/main.js
#   ./pkg/c_demo.js, c_demo.wasm, cpp_demo.js, cpp_demo.wasm
COPY web/ /var/www/wasm-c-demo/
COPY pkg/ /var/www/wasm-c-demo/pkg/

# Our single server block
RUN printf 'server {\n\
    listen 80;\n\
    server_name _;\n\
    root /var/www/wasm-c-demo;\n\
    index index.html;\n\
\n\
    location / {\n\
        # SPA-style fallback; if you only want static, use: try_files $uri =404;\n\
        try_files $uri $uri/ /index.html;\n\
    }\n\
\n\
    location ~* \\.(wasm|js)$ {\n\
        add_header Cache-Control "public, max-age=31536000, immutable";\n\
        try_files $uri =404;\n\
    }\n\
}\n' > /etc/nginx/conf.d/wasm_site.conf

EXPOSE 80

CMD ["nginx", "-g", "daemon off;"]

