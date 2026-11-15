#!/usr/bin/env bash
# Sourced by ci/*.deps.sh and run directly by .github/workflows/bsd.yml, so it
# cannot rely on the caller being strict: src/addons/CMakeLists.txt globs
# whatever is present, so a failed clone or checkout yields a green build of the
# wrong tree.

(
set -euo pipefail

CI_PLATFORM="${1:-}"
case "${CI_PLATFORM}" in
  LINUX | MACOS | WIN32 | WASM | FREEBSD) ;;
  *)
    echo "common.deps.sh: missing or unknown platform '${CI_PLATFORM}'" >&2
    echo "  usage: source ci/common.deps.sh <LINUX|MACOS|WIN32|WASM|FREEBSD>" >&2
    exit 1
    ;;
esac

cd src/addons

# Containers and the FreeBSD NFS share run git as another uid than the checkout owner.
if [[ -n "${GITHUB_ACTIONS:-}${CI:-}" ]]; then
  git config --global --add safe.directory '*'
fi

REQUESTED=()

checkout_ref() {
  local url=${1} ref=${2}

  local depth=()
  if [[ -n "${GITHUB_ACTIONS:-}${CI:-}" ]]; then depth=(--depth 1); fi
  if ! git fetch --force ${depth[@]+"${depth[@]}"} origin "${ref}"; then
    echo "error: ${url}: no such ref '${ref}' on the remote" >&2
    return 1
  fi
  git checkout --force --detach FETCH_HEAD
  if [[ -n "${NO_SUBMODULES:-}" ]]; then return 0; fi
  # The ref may add or move submodules relative to the default branch that was
  # cloned recursively above: sync them or the addon builds with the wrong trees.
  git submodule sync --recursive
  git submodule update --init --recursive --jobs 16 ${depth[@]+"${depth[@]}"}
}

clone_addon() {
  local url=${1}
  local ref=${2:-}
  local folder
  # awk takes the last path component verbatim, .git suffix included.
  folder=$(echo "${url}" | awk -F'/' '{print $NF}')
  folder=${folder%.git}

  REQUESTED+=("${folder}|${ref}|${url}")

  # Shallow in CI (throwaway checkouts); full history locally so git blame works.
  local shallow=()
  if [[ -n "${GITHUB_ACTIONS:-}${CI:-}" ]]; then
    shallow=(--depth 1 --shallow-submodules)
  fi
  # ${arr[@]+"${arr[@]}"}: bash 3.2 (/bin/bash on macOS) sees "${empty[@]}" as unbound under -u.

  (
  if [[ ! -d "$folder" ]]; then
    if [[ -n "${NO_SUBMODULES:-}" ]]; then
      # An addon that vendors whole SDKs it only needs for its own standalone
      # build: score compiles the half that links into it, which is written not
      # to need them. Clone the top level and leave the submodules alone.
      local ndepth=(); [[ ${#shallow[@]} -gt 0 ]] && ndepth=(--depth 1)
      git clone ${ndepth[@]+"${ndepth[@]}"} "$url" "$folder"
    elif [[ -n "${SKIP_SUBMODULE:-}" ]]; then
      # Skip a heavy nested submodule score never compiles (SKIP_SUBMODULE is
      # "<super-path> <submodule-name>"): clone, init that super, mark it none, recurse.
      local sdepth=()
      if [[ ${#shallow[@]} -gt 0 ]]; then
        sdepth=(--depth 1)
      fi
      git clone ${sdepth[@]+"${sdepth[@]}"} "$url" "$folder"
      (
        cd "$folder"
        git submodule update --init ${sdepth[@]+"${sdepth[@]}"} "${SKIP_SUBMODULE%% *}"
        git -C "${SKIP_SUBMODULE%% *}" config "submodule.${SKIP_SUBMODULE#* }.update" none
        git submodule update --init --recursive ${sdepth[@]+"${sdepth[@]}"}
      )
    else
      git clone --recursive -j16 ${shallow[@]+"${shallow[@]}"} "$url" "$folder"
    fi

    if [[ -n "${ref}" ]]; then
      cd "$folder"
      checkout_ref "$url" "$ref"
    fi
  else
    # Try to update the addon if it's really super clean
    cd "$folder"
    # NO_SUBMODULES applies to an existing checkout too, or a second run pulls in
    # the SDKs the first deliberately skipped.
    if [[ -n "${NO_SUBMODULES:-}" ]]; then
      git update-index --really-refresh || true
      if [[ -n "${ref}" ]]; then
        if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
          echo "error: ${folder} has local modifications, refusing pinned checkout" >&2
          exit 1
        fi
        checkout_ref "$url" "$ref"
      else
        git pull --ff-only || echo "note: could not update ${folder}, using the local checkout" >&2
      fi
      exit 0
    fi
    git update-index --really-refresh || true
    if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
      if [[ -n "${ref}" ]]; then
        echo "error: ${folder} has local modifications, refusing to check out '${ref}'" >&2
        exit 1
      fi
      echo "note: ${folder} has local modifications, leaving it as-is" >&2
    elif [[ -n "${ref}" ]]; then
      checkout_ref "$url" "$ref"
    else
      # checkout_ref leaves a detached HEAD, which "git pull" will not move.
      if ! git symbolic-ref -q HEAD > /dev/null; then
        local head_branch
        head_branch=$(git symbolic-ref -q --short refs/remotes/origin/HEAD 2>/dev/null)
        head_branch=${head_branch#origin/}
        if [[ -z "${head_branch}" ]]; then
          head_branch=$(git remote show origin 2>/dev/null | sed -n 's/.*HEAD branch: //p' | head -1)
        fi
        if [[ -n "${head_branch}" ]] && git fetch --force origin "${head_branch}"; then
          echo "note: ${folder} was detached at $(git rev-parse --short HEAD), moving it onto ${head_branch}" >&2
          git checkout --force -B "${head_branch}" FETCH_HEAD
        else
          echo "note: ${folder} is detached and its default branch could not be resolved, using the local checkout" >&2
        fi
      fi
      git pull --ff-only || echo "note: could not update ${folder}, using the local checkout" >&2
      git submodule sync --recursive
      git submodule update --init --recursive --jobs 16
    fi
  fi
  )
}

verify_addons() {
  local failed=()
  local entry folder ref url head want

  echo "score add-ons:"
  for entry in ${REQUESTED[@]+"${REQUESTED[@]}"}; do
    folder=${entry%%|*}
    ref=${entry#*|}; ref=${ref%%|*}
    url=${entry##*|}

    if [[ ! -d "${folder}" ]]; then
      failed+=("${folder}: not cloned")
      continue
    fi
    if [[ ! -f "${folder}/CMakeLists.txt" ]]; then
      failed+=("${folder}: incomplete clone, no CMakeLists.txt")
      continue
    fi

    head=$(git -C "${folder}" rev-parse HEAD)
    if [[ -n "${ref}" ]]; then
      want=$(git ls-remote "${url}" "${ref}" | awk 'NR == 1 { print $1 }')
      want=${want:-${ref}}
      if [[ "${head}" != "${want}"* ]]; then
        failed+=("${folder}: on ${head}, expected ${want} (${ref})")
        continue
      fi
    fi
    printf '  %-34s %s %s\n' "${folder}" "${head:0:12}" "${ref}"
  done

  if [[ ${#failed[@]} -gt 0 ]]; then
    echo "error: the following add-ons were not set up correctly:" >&2
    printf '  %s\n' "${failed[@]}" >&2
    return 1
  fi
}

# Add-on API baseline from the official Score 3.9.0 Windows release:
# https://github.com/ossia/score/actions/runs/38803100500
clone_addon https://github.com/ossia/iscore-addon-network acbbc700c339e44d43cfea8bda8a8902662f4317
# Match the Avendish API shipped with Score 3.9.0.
clone_addon https://github.com/ossia/score-addon-synthimi e85b4f2772e4d9e70b6a1c6d0eb75d1f5bdfa5ca
clone_addon https://github.com/ossia/score-addon-jk ce133d1c6ff4a3b4d2021ded86ee04d8ab2851ca
clone_addon https://github.com/ossia/GBAP 436f08461a6bfdb4f0cda602ffe94e1da41d4339
clone_addon https://github.com/ossia/score-addon-ltc e5b0a4042d8ca91b7d632efb2297d6ff7965d1cd
clone_addon https://github.com/bltzr/score-avnd-granola a45445af878ba2f17eab10beb01165c5eb3c8562
clone_addon https://github.com/jcelerier/bendage a3e47af0dc658a9d810ec173812de82d565cc38f
# libs/airwindows is ~600MB of regeneration-only sources; score compiles the
# committed src/autogen_airwin, so skip it.
SKIP_SUBMODULE="3rdparty/airwin2rack libs/autoexport_airwin/airwindows" \
  clone_addon https://github.com/ossia/score-addon-airwindows b04e9211405f7ef4ab67ea627ef3d31d0e1ef4ef
clone_addon https://github.com/ossia/score-addon-cv 5969d0eed760a8ecfa7b9c51033f8d5358c66ff5
clone_addon https://github.com/ossia/score-addon-onnx 7bc4d73339e43fd3285ea468f47f43d70cc42053
clone_addon https://github.com/ossia/score-addon-puara 148c8e39cf24b4bf4670cc02f685847dbe089ad2
clone_addon https://github.com/ossia/score-addon-deuterium 0eb9d9a4591c589b55bc41a907dc659347b0a3f3

if [[ "$CI_PLATFORM" != "WASM" ]];
then
  clone_addon https://github.com/ossia/score-addon-ble 3e773701107290a0c656a629a5afcd6ed90062b7
  clone_addon https://github.com/ossia/score-addon-contextfree 35563beb66afc0c07e1c51148fe714ebcf9721d1
  clone_addon https://github.com/ossia/score-addon-hdf5 bc00aabc0a04a0eba3c9e002ac42284d75841c5a
  clone_addon https://github.com/ossia/score-addon-led 67498b478fc0bafad3ef54a23340329fdcffc3bf
  clone_addon https://github.com/ossia/score-addon-lavfi 86442727b7a0d23598d7f45893ac9163b29026ff
  clone_addon https://github.com/ossia/score-addon-lsl 717ad94c7fe55bc09f0a34ea64e5cfd435841dbb
  clone_addon https://github.com/ossia/score-addon-ndi c80a7964b31a8f518c6872d369036277229165d5
  clone_addon https://github.com/ossia/score-addon-openzen ef4e3c7e1fbaa4a8d644f3edc299211f43c9c250
  clone_addon https://github.com/ossia/score-addon-spatgris 90a413205b302b8dc241c560ad996bc26a6fcccf
  clone_addon https://github.com/ossia/score-addon-ultraleap 0092e2aea62f49ac0a443ce934989b2240f00f2d
  clone_addon https://github.com/ossia/score-addon-sysinfo 5daebc20068404d2ddac595ef3df9352c0c9524b
  clone_addon https://github.com/ossia/score-addon-trackingprotocols 43d739b3ee9c9807c4db8e09eb5fe696fbdeb953
  clone_addon https://github.com/ossia/score-addon-videoio d701eb97109d71c925ab69f333b2b74932f31d45
  # The branch tip b163900 contains unresolved CMake merge markers. Keep the
  # preceding Score-compatible revision so deployment remains reproducible.
  clone_addon https://github.com/sat-mtl/carto-tcp-avendish.git 89eaf61a15bd7e345a233edc419f2086ab5382c9
  NO_SUBMODULES=1 clone_addon https://github.com/ossia/score-addon-orbbec 6ce2038a1481074feed8b36e19d51de7d50a2615
fi

if [[ "$CI_PLATFORM" == "LINUX" || "$CI_PLATFORM" == "WIN32" ]]; then
  clone_addon https://github.com/ossia/score-addon-librediffusion 4dbb5f1ac93baf462c8971b89eebc973f6c16a5b
fi

verify_addons
)

score_addons_status=$?
if [[ ${score_addons_status} -ne 0 ]]; then
  echo "error: failed to set up the score add-ons, see above" >&2
  exit ${score_addons_status}
fi
