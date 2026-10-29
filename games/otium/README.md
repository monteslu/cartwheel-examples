# Otium

Roman trade across Italy against two rival houses, on cartwheel and three.c
(three.js r186). The map lies on a walnut table: twenty-two cities in eight
regions, roads inked across the land and sea lanes across the water. Over
six seasons you send carts and galleys out from Roma, build houses, harvest
regions, trade at the market and court the gods; whoever the gods favour
most at the end wins.

Time moves only while you act. Choosing costs nothing: the world waits.
Every order costs days (the sundial in the middle of the order wheel says
how many, and the year across the top shows where it would leave you), and
the rivals act while those days pass. You can watch what each rival is
doing: its order's icon sits over the city it is working, with a ring that
fills as it finishes.

The rules (`sim.c`) are integers, the rivals (`ai.c`) score every order
they could give on them, the map (`map.c`), the 3D side (`world.c`,
`textures.c`) and the app (`main.c`: input, HUD, audio, save).

## Controls

| | gamepad | keyboard |
|---|---|---|
| move the gold ring between cities | stick or d-pad | WASD or the arrows |
| open the order wheel, give the order | A | Space or Enter |
| back | B | Esc |
| let time run without acting | hold RT (or Y) | hold Tab or Shift |
| choose which cart a move sends (else the nearest goes) | LB / RB | Q / E |
| the gods' ledger | hold Back | hold I |
| zoom | right stick | + / - |
| pause (graphics settings there) | START | P |

The mouse works too: point at a city, click to open the wheel, click a slot.
An order chosen while you are still busy waits its turn and goes the moment
you are free.

## The orders

| | | days |
|---|---|---|
| move | send a cart along the roads (it takes ship at the sea lanes) to the city under the ring | the road's |
| build | a house in every city where one of your carts stands, as far as your goods and coins go | 3 |
| harvest | every house in the ring's region makes its city's good, whoever owns it; you get the region's bonus good too; the region then rests until the next season | 4 |
| trade | the market pays you 3 coins, then buy or sell up to three goods at their fixed values | 2 |
| recruit | a new cart, for a food and a tool | 3 |
| temple | one of three offers: a god's favour for goods and coins | 3 |
| rest | every order you have used comes back, and you get a coin for each, plus one | 3 |

Each order but move can be used once until you rest.

Goods and their values: brick 3, food 4, tools 5, wine 6, cloth 7. A house
costs a food in a brick city, else a brick and the city's own good; and 1
to 5 coins by the city's good, times the number of houses there once yours
is built. The storehouse holds twelve goods.

## Scoring

Each god counts one thing and multiplies it by your favour with him (every
house starts with one of each):

| god | counts |
|---|---|
| Jupiter (the thunderbolt) | your houses outside the brickworks |
| Saturn (the sickle) | regions you have a house in, two each |
| Mercury (the winged hat) | kinds of goods your houses make, two each |
| Mars (the shield) | your carts and galleys, three each |
| Vesta (the flame) | your coins and goods at their values, a point per three |

## Tests

`otium_auto` plays every house itself. The gates in `tests/gates.json`
(`otium-title`, `otium-orders`, `otium-auto`) hold every host to the same
named state; `otium-orders` plays a game by pad: a move, a harvest, a sale
at the market, a rest and a held wait. `tools/soak.c` plays bot-only games
headless and reports how often each rival personality wins (every one of
them should, some of the time):

    cc -O1 -std=c11 -ffp-contract=off -Igames/otium -Ideps/cartwheel-sdk/linux-x64/include \
      -Ideps/cartwheel-engine/deps/three.c/include -include stdlib.h games/otium/tools/soak.c \
      games/otium/sim.c games/otium/ai.c games/otium/map.c deps/cartwheel-sdk/linux-x64/lib/libcartwheel.a -lm -o out/otium-soak
    out/otium-soak 90
