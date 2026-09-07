#!/bin/sh

PATH=/bin:/usr/bin
TERM=screen

shell=
if command -v bash >/dev/null 2>&1; then
	# If Bash is available, we start a plain Bash session (without any user
	# configuration files) for testing.
	#
	# Note: We disable the command history by passing "+o history". If an
	# interactive Bash session is started without any configuration files,
	# the user's command history may be truncated to the default maximum
	# size of 500. To avoid breaking the user's command history, we disable
	# the command history.
	shell='bash --noprofile --norc +o history'
fi

[ -z "$TEST_TERMO" ] && TEST_TERMO="${TEST_TMUX:-$(readlink -f ../../build/termo 2>/dev/null || readlink -f ../termo 2>/dev/null || which termo)}"
TEST_TMUX="$TEST_TERMO"
TERMO="$TEST_TERMO -LtestA$$ -f/dev/null"
TMUX="$TERMO"
$TERMO kill-server 2>/dev/null
TERMO2="$TEST_TERMO -LtestB$$ -f/dev/null"
TMUX2="$TERMO2"
$TERMO2 kill-server 2>/dev/null

$TERMO2 -f/dev/null new -d "$TERMO -f/dev/null new -- $shell"
sleep 2
$TERMO set -g status-style fg=default,bg=default

check() {
	v=$($TERMO display -p "$1")
	$TERMO set -g status-format[0] "$1"
	sleep 1
	r=$($TERMO2 capturep -Cep|tail -1|sed 's|\\033\[||g')

	if [ "$v" != "$2" -o "$r" != "$3" ]; then
		printf "$1 = [$v = $2] [$r = $3]"
		printf " \033[31mbad\033[0m\n"
		exit 1
	fi
}

# drawn as #0
$TERMO setenv -g V '#0'
check '#{V} #{w:V}' '#0 2' '#0 2'
check '#{=3:V}' '#0' '#0'
check '#{=-3:V}' '#0' '#0'

# drawn as #0
$TERMO setenv -g V '###[bg=yellow]0'
check '#{V} #{w:V}' '###[bg=yellow]0 2' '#43m0 249m'
check '#{=3:V}' '###[bg=yellow]0' '#43m049m'
check '#{=-3:V}' '###[bg=yellow]0' '#43m049m'

# drawn as #0123456
$TERMO setenv -g V '#0123456'
check '#{V} #{w:V}' '#0123456 8' '#0123456 8'
check '#{=3:V}' '#01' '#01'
check '#{=-3:V}' '456' '456'

# drawn as #0123456
$TERMO setenv -g V '##0123456'
check '#{V} #{w:V}' '##0123456 8' '#0123456 8'
check '#{=3:V}' '##01' '#01'
check '#{=-3:V}' '456' '456'

# drawn as ##0123456
$TERMO setenv -g V '###0123456'
check '#{V} #{w:V}' '###0123456 9' '##0123456 9'
check '#{=3:V}' '####0' '##0'
check '#{=-3:V}' '456' '456'

# drawn as 0123456
$TERMO setenv -g V '#[bg=yellow]0123456'
check '#{V} #{w:V}' '#[bg=yellow]0123456 7' '43m0123456 749m'
check '#{=3:V}' '#[bg=yellow]012' '43m01249m'
check '#{=-3:V}' '#[bg=yellow]456' '43m45649m'

# drawn as #[bg=yellow]0123456
$TERMO setenv -g V '##[bg=yellow]0123456'
check '#{V} #{w:V}' '##[bg=yellow]0123456 19' '#[bg=yellow]0123456 19'
check '#{=3:V}' '##[b' '#[b'
check '#{=-3:V}' '456' '456'

# drawn as #0123456
$TERMO setenv -g V '###[bg=yellow]0123456'
check '#{V} #{w:V}' '###[bg=yellow]0123456 8' '#43m0123456 849m'
check '#{=3:V}' '###[bg=yellow]01' '#43m0149m'
check '#{=-3:V}' '#[bg=yellow]456' '43m45649m'

# drawn as ##[bg=yellow]0123456
$TERMO setenv -g V '####[bg=yellow]0123456'
check '#{V} #{w:V}' '####[bg=yellow]0123456 20' '##[bg=yellow]0123456 20'
check '#{=3:V}' '####[' '##['
check '#{=-3:V}' '456' '456'

# drawn as ###0123456
$TERMO setenv -g V '#####[bg=yellow]0123456'
check '#{V} #{w:V}' '#####[bg=yellow]0123456 9' '##43m0123456 949m'
check '#{=3:V}' '#####[bg=yellow]0' '##43m049m'
check '#{=-3:V}' '#[bg=yellow]456' '43m45649m'

$TERMO kill-server 2>/dev/null
$TERMO2 kill-server 2>/dev/null
exit 0
