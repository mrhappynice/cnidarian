docker create --name cnidarian-build-cont cnidarian-build && \
docker cp cnidarian-build-cont:/app/pkg ./pkg && \
docker rm cnidarian-build-cont
