/* 60/16-inch ordinary: curved backbone, rigid fork and direct-drive hub.
 * Dimensions share the physics/rider fit; there is no bottom bracket or chain. */
static t3_object *build_penny_frame(const mats *m, t3_geometry *cyl, bike_fit fit) {
  t3_object *f=t3_group_new();
  t3_vec2 crown={-.015f,.80f}, rear={fit.rear_axle.x,fit.rear_axle.y};
  /* The backbone follows the back of the big wheel, then splits at the tail. */
  bent_tube(f,cyl,m->paint,crown,(t3_vec2){-.72f,.79f},(t3_vec2){-.94f,-.23f},.025f,0,24);
  for(int side=-1;side<=1;side+=2) {
    bent_tube(f,cyl,m->paint,(t3_vec2){-.94f,-.23f},(t3_vec2){-1.05f,-.39f},rear,.014f,side*.045f,10);
    tube(f,cyl,m->alloy,(t3_vec2){-.34f,.76f},(t3_vec2){-.085f,.79f},.006f,side*.05f);
  }
  tube(f,cyl,m->paint,(t3_vec2){-.20f,.68f},(t3_vec2){fit.saddle.x,fit.saddle.y-.025f},.017f,0);
  t3_geometry *sad=t3_sphere_geometry_new(1,24,12);
  t3_mesh *seat=mesh(f,sad,m->saddle);
  seat->base.position=(t3_vec3){fit.saddle.x,fit.saddle.y,0};
  seat->base.scale=(t3_vec3){.145f,.027f,.105f};
  t3_mesh *nose=mesh(f,sad,m->saddle);
  nose->base.position=(t3_vec3){fit.saddle.x+.115f,fit.saddle.y-.007f,0};
  nose->base.scale=(t3_vec3){.08f,.018f,.035f};
  /* Brass saddle rivets and the rear mounting step. */
  for(int side=-1;side<=1;side+=2)for(int i=0;i<3;i++) {
    t3_mesh *r=mesh(f,sad,m->alloy);
    r->base.position=(t3_vec3){fit.saddle.x-.09f+i*.045f,fit.saddle.y+.015f,side*.075f};
    r->base.scale=(t3_vec3){.004f,.003f,.004f};
  }
  t3_release(sad);
  tube3(f,cyl,m->alloy,(t3_vec3){-.98f,-.27f,-.06f},(t3_vec3){-.98f,-.27f,.17f},.012f);
  t3_geometry *step=t3_box_geometry_new(.10f,.018f,.10f,1,1,1);
  t3_mesh *st=mesh(f,step,m->black);st->base.position=(t3_vec3){-.98f,-.26f,.15f};t3_release(step);
  return f;
}

/* The fork, bars and front-hub cranks steer as a unit about the head axis. */
static t3_object *build_penny_fork(const mats *m,t3_geometry *cyl) {
  t3_object *f=t3_group_new();
  for(int side=-1;side<=1;side+=2) {
    tube(f,cyl,m->paint,(t3_vec2){0,.80f},(t3_vec2){0,.08f},.019f,side*.052f);
    tube(f,cyl,m->alloy,(t3_vec2){0,.08f},(t3_vec2){0,0},.016f,side*.052f);
    t3_vec3 a={0,.95f,0},b={.10f,1.02f,side*.12f},c={.19f,1.00f,side*.20f},d={.18f,.98f,side*.27f};
    tube3(f,cyl,m->alloy,a,b,.012f);tube3(f,cyl,m->alloy,b,c,.012f);
    tube3(f,cyl,m->saddle,c,d,.018f);
  }
  tube(f,cyl,m->alloy,(t3_vec2){0,.77f},(t3_vec2){0,.95f},.019f,0);
  return f;
}
