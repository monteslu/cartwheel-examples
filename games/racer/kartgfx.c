/* racer's karts as drawn: a node per kart placed from the sim on the fixed tick (rendered between the last two
 * ticks), its wheels posed from the vehicle controller. First pass: the UI of a kart's body motion, spin-outs,
 * boost flames and the shield belong here. */
#include <string.h>

#include "cartwheel/math.h"
#include "world.h"

static struct {
  kart_model km[MAXK];
  cw_node node[MAXK];
} K;

cw_node kartgfx_node(int i) { return K.node[i]; }

void kartgfx_reset(void) { memset(&K, 0, sizeof K); }

void kartgfx_build(cw_node parent, const sim *s) {
  kartgfx_reset();
  for (int i = 0; i < MAXK; i++) {
    const kart *k = &s->k[i];
    if (!k->on) continue;
    kart_build(&K.km[i], k->veh, k->colour);
    K.node[i] = hang(parent, "Kart", K.km[i].root);
    cw_node3d_set_position(K.node[i], k->p);
    cw_node3d_set_quaternion(K.node[i], k->q);
  }
}

void kartgfx_tick(const sim *s) {
  for (int i = 0; i < MAXK; i++) {
    const kart *k = &s->k[i];
    if (!k->on || !cw_node_valid(K.node[i])) continue;
    cw_node n = K.node[i];
    cw_node3d_snapshot(n);
    cw_node3d_set_position(n, k->p);
    cw_node3d_set_quaternion(n, k->q);
    for (int w = 0; w < 4; w++) {
      t3_object *wo = K.km[i].wheel[w];
      if (!wo) continue;
      cw_xform3 lw = cw_vehicle3d_wheel_local(sim_car(s, i), w, 1.0f);
      t3_object_set_position(wo, lw.p.x, lw.p.y, lw.p.z);
      t3_object_set_quaternion(wo, lw.q);
    }
  }
}
