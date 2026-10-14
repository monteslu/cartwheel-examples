// pugball's gate menu prefixes (run-cart input entries, pad 1 = "buttons"):
// shared by tools/make-gate-script.mjs and tools/search-goal.mjs
export const press = (frame, buttons) => [{ frame, buttons }, { frame: frame + 4, buttons: [] }];
// title -> Exhibition -> the team sheet: you (blue 1) against a CPU Rookie
// (red 1), the other four slots emptied -> KICK OFF -> stadium 1
export const MENU_1V1 = [
  ...press(30, ['south']),
  ...press(40, ['down']), ...press(48, ['left']),
  ...press(56, ['down']), ...press(64, ['left']), ...press(72, ['left']), ...press(80, ['left']),
  ...press(88, ['down']), ...press(96, ['left']), ...press(104, ['left']), ...press(112, ['left']),
  ...press(120, ['start']),
  ...press(140, ['south']),
];
// title -> Exhibition -> blue 1 becomes a CPU All-Star (2v2 CPUs) -> stadium 1
export const MENU_CPU = [
  ...press(30, ['south']),
  ...press(40, ['right']), ...press(48, ['right']), ...press(56, ['right']),
  ...press(70, ['start']),
  ...press(90, ['south']),
];
