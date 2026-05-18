# SPDX-License-Identifier: Apache-2.0
#
# Optional board-tied MERLIN-V helpers for the ESP32-C3-DevKitM-1.
# Apps that want LED-driving helpers add this directory's source to
# their `app` target:
#
#   target_sources(app PRIVATE
#     ../../helpers/helpers_esp32c3.c
#   )
#
# (The sample-classifier doesn't use these to keep the bring-up demo
# self-contained.)
