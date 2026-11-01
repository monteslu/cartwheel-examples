/* 24-inch direct-drive unicycle. Saddle and axle use the same fit as physics. */
static t3_object *build_unicycle_frame(const mats *m,t3_geometry *cyl,bike_fit fit) {
  t3_object *f=t3_group_new();
  for(int side=-1;side<=1;side+=2) {
    bent_tube(f,cyl,m->paint,(t3_vec2){0,.38f},(t3_vec2){-.035f,.32f},(t3_vec2){0,.04f},.018f,side*.066f,16);
    tube(f,cyl,m->alloy,(t3_vec2){0,.04f},(t3_vec2){0,0},.024f,side*.066f);
    tube3(f,cyl,m->paint,(t3_vec3){0,.38f,side*.066f},(t3_vec3){0,.40f,0},.019f);
  }
  tube(f,cyl,m->paint,(t3_vec2){0,.39f},(t3_vec2){0,.49f},.024f,0);
  tube(f,cyl,m->alloy,(t3_vec2){0,.46f},(t3_vec2){fit.saddle.x,fit.saddle.y},.017f,0);
  t3_geometry *ring=t3_torus_geometry_new(.025f,.006f,8,24,6.2831853f);
  t3_mesh *clamp=mesh(f,ring,m->black);clamp->base.position.y=.49f;t3_object_set_rotation(&clamp->base,1.5707963f,0,0);t3_release(ring);
  t3_geometry *ball=t3_sphere_geometry_new(1,24,16);
  t3_mesh *seat=mesh(f,ball,m->saddle);seat->base.position=(t3_vec3){0,fit.saddle.y,0};seat->base.scale=(t3_vec3){.17f,.035f,.10f};
  for(int side=-1;side<=1;side+=2){t3_mesh *end=mesh(f,ball,m->black);end->base.position=(t3_vec3){side*.135f,fit.saddle.y+.018f,0};end->base.scale=(t3_vec3){.065f,.045f,.075f};}
  t3_release(ball);return f;
}
