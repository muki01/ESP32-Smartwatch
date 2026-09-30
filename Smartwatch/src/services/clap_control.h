/*
 * clap_control.h - A double clap toggles the WLED lights (Settings > Extras > Clap
 * control). The microphone listens only while the feature is on, the Lights extra has
 * devices and Wi-Fi is connected.
 */
#pragma once

void clap_control_init();
bool clap_control_listening();
