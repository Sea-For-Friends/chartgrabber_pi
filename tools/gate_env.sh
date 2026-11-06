# Sourced by tools/check.sh: gate priority. Defines cg_gate_priority; changes nothing when sourced.

# cg_gate_priority: resolve CG_GATE_PRIORITY | ~/.config/chart-grabber/gate-priority | low, and when low lower the
# CURRENT shell process once (nice 19, ionice idle, SCHED_IDLE), so every child inherits it.
cg_gate_priority() {
    local want=${CG_GATE_PRIORITY:-} file=${XDG_CONFIG_HOME:-$HOME/.config}/chart-grabber/gate-priority
    if [[ -z $want && -r $file ]]; then
        want=$(head -n 1 "$file" | tr -d '[:space:]')
    fi
    case ${want:-low} in
    low) ;;
    normal)
        echo "gate priority: normal"
        return 0
        ;;
    *)
        echo "check.sh: warning: unknown gate priority '$want' (low|normal); using low" >&2
        ;;
    esac
    local parts=()
    renice -n 19 -p $$ >/dev/null 2>&1 && parts+=("nice 19")
    if ionice -c 3 -p $$ >/dev/null 2>&1; then
        parts+=("ionice idle")
    elif ionice -c 2 -n 7 -p $$ >/dev/null 2>&1; then
        parts+=("ionice best-effort 7")
        echo "check.sh: note: ionice idle refused, using -c 2 -n 7" >&2
    fi
    chrt --idle -p 0 $$ >/dev/null 2>&1 && parts+=("SCHED_IDLE")
    local desc=${parts[0]:-none} i
    for i in "${parts[@]:1}"; do desc+=", $i"; done
    echo "gate priority: low ($desc)"
}
