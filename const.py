DOMAIN = "etbus"

MULTICAST_GROUP = "239.10.0.1"
MULTICAST_PORT = 5555

# Hub multicast ping interval (devices learn hub IP from this)
PING_INTERVAL = 10

# IMPORTANT: Match Arduino heartbeat (PONG_INTERVAL_MS = 30000)
# Rule: OFFLINE_TIMEOUT >= 3 * pong interval seconds
# 3 * 30s = 90s, plus buffer => 95s
OFFLINE_TIMEOUT = 95
