#include "assets/battle_data.hpp"

#include <cstdio>

int main() {
    int problems = 0;

    const cr::BattleAnim noWindow{};
    problems += noWindow.hasMoveWindow();

    cr::BattleAnim startsAtZero;
    startsAtZero.moveStart = 0;
    startsAtZero.moveEnd = 12;
    problems += !startsAtZero.hasMoveWindow();

    cr::BattleAnim delayedWindow;
    delayedWindow.moveStart = 7;
    delayedWindow.moveEnd = 14;
    problems += !delayedWindow.hasMoveWindow();

    cr::BattleAnim invalidWindow;
    invalidWindow.moveStart = 14;
    invalidWindow.moveEnd = 7;
    problems += invalidWindow.hasMoveWindow();

    // btlact_attack_init: the action must move (motion 1) at the other side, and the model have a window.
    problems += cr::battleApproachEnabled(0, true, startsAtZero);
    problems += cr::battleApproachEnabled(1, false, startsAtZero);
    problems += !cr::battleApproachEnabled(1, true, startsAtZero);
    problems += cr::battleApproachEnabled(1, true, noWindow);
    problems += cr::battleApproachEnabled(1, true, invalidWindow);
    problems += !cr::battleApproachEnabled(1, true, delayedWindow);

    std::printf("battle approach: independent action motion + model timing window; %d problems\n", problems);
    return problems ? 1 : 0;
}
