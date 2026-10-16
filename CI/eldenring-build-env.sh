#!/usr/bin/env bash
# Source this file to use the locally installed native validation toolchain.
# No system packages or shell startup files are modified.
if [[ -n "${BASH_VERSION:-}" ]]; then
    _elden_env_source="${BASH_SOURCE[0]}"
elif [[ -n "${ZSH_VERSION:-}" ]]; then
    _elden_env_source="${(%):-%x}"
else
    printf '%s\n' 'Source this environment file from Bash or Zsh.' >&2
    return 1 2>/dev/null || exit 1
fi
_elden_tools_dir="$(cd -- "$(dirname -- "$_elden_env_source")/../build-tools-local" && pwd)"
unset _elden_env_source
if [[ ! -x "$_elden_tools_dir/bin/c++" || ! -x "$_elden_tools_dir/clang/bin/clang-18" || ! -x "$_elden_tools_dir/cmake/bin/cmake" ]]; then
    printf '%s\n' 'The local OpenMW toolchain installation is incomplete.' >&2
    unset _elden_tools_dir
    return 1 2>/dev/null || exit 1
fi
export PATH="$_elden_tools_dir/bin:$_elden_tools_dir/cmake/bin:$_elden_tools_dir/ninja/linux-x86/bin:$PATH"
export CC="$_elden_tools_dir/bin/cc"
export CXX="$_elden_tools_dir/bin/c++"
export LD_LIBRARY_PATH="$_elden_tools_dir/clang/lib:$_elden_tools_dir/clang/lib/x86_64-unknown-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
unset _elden_tools_dir
