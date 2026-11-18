cd "$(dirname "$0")"

# catalog.h, raft_config.h, raftlog.c and raftnode.c are shared with the STM32s and live with their code.
# Optional overrides, same as the STM32 builds (e.g. LEADER=random for checkoff 8):
#   RAFT_DEFAULT_LEADER=0 RAFT_SEED=5 sh build.sh
DEFS=""
[ -n "$RAFT_DEFAULT_LEADER" ] && DEFS="$DEFS -DRAFT_DEFAULT_LEADER=$RAFT_DEFAULT_LEADER"
[ -n "$RAFT_SEED" ] && DEFS="$DEFS -DRAFT_SEED=$RAFT_SEED"

# publisher.c (cloud mirror) needs hiredis: sudo apt install libhiredis-dev
gcc -Wall -pthread -I../../stm32-common/include $DEFS receiver.c publisher.c ../../stm32-common/src/raftlog.c ../../stm32-common/src/raftnode.c -o proxy_receiver -lpigpio -lrt -lhiredis
