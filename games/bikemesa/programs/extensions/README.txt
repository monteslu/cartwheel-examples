Hellfire project shader

From the cartwheel-examples repository root, regenerate BOTH checked-in tables:

node ../cartwheel-engine/tools/programs.mjs --table games/bikemesa/programs/bikemesa.txt --build build/djbike-cart --extend games/bikemesa/programs/extensions/helllava.mjs

The generic engine programs target does not pass project extensions. Use the
command above when changing this shader or the BikeMesa material manifest.
Ordinary cart builds consume the checked-in tables and need no shader generator.
