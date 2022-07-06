#!/usr/bin/env bash
#
# Copyright (C) 2022 Open Source Robotics Foundation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# bash tab-completion

# This is a per-library function definition, used in conjunction with the
# top-level entry point in ign-tools.

GZ_SIM_COMPLETION_LIST="
  -g
  --iterations
  --levels
  --network-role
  --network-secondaries
  --record
  --record-path
  --record-resources
  --record-topic
  --log-overwrite
  --log-compress
  --playback
  -r
  -s
  -v --verbose
  --gui-config
  --physics-engine
  --render-engine
  --render-engine-gui
  --render-engine-server
  --version
  -z
  -h --help
  --force-version
  --versions
"

# TODO(anyone): In Garden+, replace `gazebo` in function name to align with
# subcommand, for ign-tools/etc/ign.bash_completion.sh to find this function.
function _gz_gazebo
{
  if [[ ${COMP_WORDS[COMP_CWORD]} == -* ]]; then
    # Specify options (-*) word list for this subcommand
    # TODO(anyone): In Fortress+, add --headless-rendering.
    # Update ../ign_TEST.cc accordingly.
    COMPREPLY=($(compgen -W "$GZ_SIM_COMPLETION_LIST" \
      -- "${COMP_WORDS[COMP_CWORD]}" ))
    return
  else
    # Just use bash default auto-complete, because we never have two
    # subcommands in the same line. If that is ever needed, change here to
    # detect subsequent subcommands
    COMPREPLY=($(compgen -o default -- "${COMP_WORDS[COMP_CWORD]}"))
    return
  fi
}

function _gz_sim_flags
{
  for word in $GZ_SIM_COMPLETION_LIST; do
    echo "$word"
  done
}
