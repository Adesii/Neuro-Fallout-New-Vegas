default:
  @just --list

debug:
  bash tools/build.sh Debug

rebuild:
  bash tools/build.sh Debug --clean-first

rebuild-debug:
  bash tools/build.sh Debug --clean-first

release:
  bash tools/build.sh Release
