/* Adult pedal trike: low bucket seat, 26-inch driven front and twin 12-inch rear. */
static t3_object *build_bigwheel_frame(const mats *m,t3_geometry *cyl,bike_fit fit) {
  t3_object *f=t3_group_new();
  bent_tube(f,cyl,m->paint,(t3_vec2){-.94f,-.18f},(t3_vec2){-.45f,-.24f},(t3_vec2){-.27f,-.06f},.031f,0,18);
  bent_tube(f,cyl,m->paint,(t3_vec2){-.27f,-.06f},(t3_vec2){-.16f,.03f},(t3_vec2){-.035f,.40f},.029f,0,16);
  tube3(f,cyl,m->alloy,(t3_vec3){-.94f,-.1778f,-.39f},(t3_vec3){-.94f,-.1778f,.39f},.024f);
  for(int side=-1;side<=1;side+=2)tube3(f,cyl,m->paint,(t3_vec3){-.57f,-.14f,0},(t3_vec3){-.94f,-.1778f,side*.28f},.022f);
  t3_geometry *seat=t3_sphere_geometry_new(1,32,16);
  t3_mesh *pan=mesh(f,seat,m->saddle);pan->base.position=(t3_vec3){fit.saddle.x,fit.saddle.y,0};pan->base.scale=(t3_vec3){.23f,.045f,.22f};
  t3_mesh *back=mesh(f,seat,m->saddle);back->base.position=(t3_vec3){-.89f,.14f,0};back->base.scale=(t3_vec3){.045f,.17f,.22f};t3_object_set_rotation(&back->base,0,0,-.22f);
  t3_release(seat);
  /* Reflectors and a rear safety pennant distinguish the low silhouette. */
  tube3(f,cyl,m->alloy,(t3_vec3){-.94f,-.14f,-.23f},(t3_vec3){-1.02f,1.13f,-.23f},.004f);
  float pos[]={-1.02f,1.13f,-.23f,-1.02f,.92f,-.23f,-.77f,1.04f,-.23f,
               -1.02f,1.13f,-.23f,-1.02f,.92f,-.23f,-.77f,1.04f,-.23f};
  float uv[]={0,1,0,0,1,.5f,0,1,0,0,1,.5f};
  uint32_t indices[]={0,1,2,5,4,3};
  t3_geometry *flag=t3_geometry_new();set_attr(flag,T3_ATTR_POSITION,t3_attribute_new(T3_FLOAT32,pos,6,3));set_index(flag,t3_attribute_new(T3_UINT32,indices,6,1));set_attr(flag,T3_ATTR_UV,t3_attribute_new(T3_FLOAT32,uv,6,2));t3_geometry_compute_vertex_normals(flag);mesh(f,flag,m->paint);t3_release(flag);
  return f;
}
static t3_object *build_bigwheel_fork(const mats *m,t3_geometry *cyl) {
  t3_object *f=t3_group_new();
  for(int side=-1;side<=1;side+=2) {
    tube(f,cyl,m->paint,(t3_vec2){-.035f,.40f},(t3_vec2){0,0},.021f,side*.065f);
    tube3(f,cyl,m->alloy,(t3_vec3){-.08f,.52f,0},(t3_vec3){-.15f,.61f,side*.14f},.014f);
    tube3(f,cyl,m->alloy,(t3_vec3){-.15f,.61f,side*.14f},(t3_vec3){-.20f,.65f,side*.29f},.014f);
    tube3(f,cyl,m->black,(t3_vec3){-.20f,.65f,side*.22f},(t3_vec3){-.20f,.65f,side*.30f},.021f);
  }
  tube(f,cyl,m->alloy,(t3_vec2){-.035f,.39f},(t3_vec2){-.08f,.52f},.020f,0);
  return f;
}
static t3_object *build_bigwheel_rear(const mats *m,t3_geometry *cyl) {
  (void)cyl;t3_object *g=t3_group_new();
  t3_geometry *tire=t3_cylinder_geometry_new(.1524f,.1524f,.16f,48,1,false);
  t3_geometry *hub=t3_cylinder_geometry_new(.06f,.06f,.166f,24,1,false);
  t3_geometry *rim=t3_torus_geometry_new(.139f,.009f,8,48,6.2831853f);
  for(int side=-1;side<=1;side+=2) {
    t3_mesh *w=mesh(g,tire,m->rubber);w->base.position.z=side*.36f;t3_object_set_rotation(&w->base,1.5707963f,0,0);
    t3_mesh *h=mesh(g,hub,m->alloy);h->base.position.z=side*.36f;t3_object_set_rotation(&h->base,1.5707963f,0,0);
    for(int j=-1;j<=1;j++) {t3_mesh *r=mesh(g,rim,m->black);r->base.position.z=side*.36f+j*.05f;}
  }
  t3_release(tire);t3_release(hub);t3_release(rim);return g;
}
