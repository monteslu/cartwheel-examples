/* Heavy-duty front-loading DJ tricycle. Dimensions in metres. The deck,
 * equipment, axle and front mudguards belong to one steering assembly. */
static void dj_box(t3_object *p, t3_material *m, float x,float y,float z,float w,float h,float d) {
  t3_geometry *g=t3_box_geometry_new(w,h,d,1,1,1);
  mesh(p,g,m)->base.position=(t3_vec3){x,y,z};t3_release(g);
}
static t3_object *build_dj_frame(const mats *m,t3_geometry *cyl,bike_fit fit) {
  t3_object *f=t3_group_new();
  t3_vec2 ra={fit.rear_axle.x,fit.rear_axle.y},seat={-.14f,.48f},nose={1.12f,.02f};
  tube(f,cyl,m->paint,ra,(t3_vec2){0,0},.023f,0);
  tube(f,cyl,m->paint,(t3_vec2){0,0},seat,.027f,0);
  tube(f,cyl,m->paint,seat,(t3_vec2){.61f,.02f},.025f,0);
  tube(f,cyl,m->paint,(t3_vec2){0,0},nose,.035f,0);
  for(int side=-1;side<=1;side+=2) {
    tube(f,cyl,m->paint,ra,seat,.016f,side*.055f);
    tube(f,cyl,m->paint,ra,(t3_vec2){0,0},.017f,side*.055f);
  }
  tube(f,cyl,m->alloy,seat,(t3_vec2){fit.saddle.x,fit.saddle.y},.022f,0);
  dj_box(f,m->saddle,fit.saddle.x,fit.saddle.y,0,.28f,.055f,.26f);
  dj_box(f,m->alloy,-.19f,-.005f,.085f,.64f,.07f,.025f);
  return f;
}
/* Preserve the supplied sticker asset unchanged. Its dark printed artwork
 * becomes white ink at load time; pastel sticker stock becomes black panel. */
static void dj_logo(t3_object *p) {
  size_t size=0;void *bytes=cw_asset_load("bikes/bikemesa-sticker.png",&size);
  t3_texture *tx=bytes?t3_texture_load_memory(bytes,size):NULL;free(bytes);
  if(!tx){cw_warn("DJ bike: missing BikeMesa sticker");return;}
  for(int i=0;i<tx->width*tx->height;i++) {
    unsigned char *px=tx->pixels+i*4;
    float hi=fmaxf(px[0],fmaxf(px[1],px[2]))/255.f;
    float ink=(1-cw_smoothstep(.15f,.40f,hi))*px[3]/255.f;
    unsigned char value=(unsigned char)(255*ink);
    px[0]=px[1]=px[2]=value;px[3]=255;
  }
  tx->version++;tx->color_space=T3_SRGB_COLOR_SPACE;
  tx->wrap_s=tx->wrap_t=T3_REPEAT;
  tx->generate_mipmaps=true;tx->min_filter=T3_LINEAR_MIPMAP_LINEAR;
  t3_material *ink=t3_mesh_standard_material_new(0xffffff);
  tex_apply(ink,TEX_COTTON,1,1,.03f,0);
  t3_material_set_map(ink,tx);ink->roughness=.65f;t3_release(tx);
  t3_geometry *g=t3_plane_geometry_new(.35f,.28f,1,1);
  for(int side=-1;side<=1;side+=2) {
    t3_mesh *logo=mesh(p,g,ink);logo->base.position=(t3_vec3){-.25f,.89f,side*.368f};
    if(side<0)t3_object_set_rotation(&logo->base,0,3.14159265f,0);
  }
  t3_release(g);
  /* Forward-facing mark above the tops of both speaker cabinets. */
  g=t3_plane_geometry_new(.30f,.24f,1,1);
  t3_mesh *front=mesh(p,g,ink);front->base.position=(t3_vec3){-.043f,1.17f,0};
  t3_object_set_rotation(&front->base,0,1.5707963f,0);
  t3_release(g);t3_release(ink);
}
static t3_object *build_dj_cargo(const mats *m,t3_geometry *cyl) {
  t3_object *p=t3_group_new();
  t3_material *violet=t3_mesh_standard_material_new(0x8035ff);
  violet->emissive=lin(.45f,.025f,1);violet->emissive_intensity=3;violet->roughness=.45f;
  t3_material *white=t3_mesh_standard_material_new(0xd0f5ff);
  white->emissive=lin(.35f,.75f,1);white->emissive_intensity=1.5f;
  /* Coordinates relative to the steering axle. */
  dj_box(p,m->black,0,.22f,0,1.05f,.055f,.86f);
  for(int side=-1;side<=1;side+=2) {
    dj_box(p,m->black,0,.28f,side*.42f,1.05f,.09f,.025f);
    tube3(p,cyl,m->alloy,(t3_vec3){-.51f,.23f,side*.32f},(t3_vec3){-.56f,1.18f,side*.25f},.018f);
    dj_box(p,violet,0,.29f,side*.437f,1.0f,.009f,.008f);
    /* Chrome fenders with a broad curved crown. */
    t3_geometry *guard=t3_torus_geometry_new(.278f,.018f,8,32,2.7f);
    t3_mesh *g=mesh(p,guard,m->alloy);g->base.position.z=side*.49f;
    g->base.scale.z=2.4f;t3_object_set_rotation(&g->base,0,0,.22f);t3_release(guard);
  }
  tube3(p,cyl,m->alloy,(t3_vec3){-.56f,1.18f,-.25f},(t3_vec3){-.56f,1.18f,.25f},.019f);
  tube3(p,cyl,m->alloy,(t3_vec3){0,0,-.49f},(t3_vec3){0,0,.49f},.025f);
  /* Two forward-facing cabinets, raised podium behind them. */
  for(int side=-1;side<=1;side+=2) {
    float z=side*.205f;
    dj_box(p,m->black,.25f,.63f,z,.43f,.75f,.39f);
    dj_box(p,m->rubber,.474f,.63f,z,.016f,.69f,.35f);
    t3_geometry *cone=t3_cylinder_geometry_new(.132f,.10f,.014f,32,1,false);
    t3_mesh *c=mesh(p,cone,m->black);c->base.position=(t3_vec3){.487f,.52f,z};
    t3_object_set_rotation(&c->base,0,0,1.5707963f);t3_release(cone);
    dj_box(p,m->black,.489f,.85f,z,.02f,.12f,.22f);
    /* Steel grille bars, cabinet corners and carrying handle. */
    for(int k=0;k<9;k++)dj_box(p,m->guard,.50f,.32f+k*.073f,z,.009f,.006f,.33f);
    dj_box(p,m->alloy,.25f,.98f,z,.39f,.018f,.36f);
    dj_box(p,m->black,.25f,.68f,z+side*.201f,.13f,.055f,.015f);
  }
  dj_box(p,m->black,-.25f,.79f,0,.40f,1.06f,.73f);
  dj_logo(p);
  t3_object *deck=t3_group_new();t3_object_add(p,deck);t3_release(deck);
  /* Front edge raised: the controls slope down towards the rider. All
   * controls and the laptop mount share the platform's tilt. */
  float slope=.16f, cx=-.25f, cy=1.205f;
  t3_object_set_rotation(deck,0,0,slope);
  deck->position=(t3_vec3){cx-cx*cw_cos(slope)+cy*cw_sin(slope),cy-cx*cw_sin(slope)-cy*cw_cos(slope)+.12f,0};
  dj_box(deck,m->alloy,-.25f,1.205f,0,.50f,.025f,.79f);
  /* FLX4 silhouette: twin jog wheels, central mixer, eight pads per deck. */
  dj_box(deck,m->black,-.27f,1.24f,0,.30f,.045f,.65f);
  for(int side=-1;side<=1;side+=2) {
    t3_geometry *jog=t3_cylinder_geometry_new(.082f,.082f,.012f,32,1,false);
    mesh(deck,jog,m->alloy)->base.position=(t3_vec3){-.28f,1.273f,side*.20f};t3_release(jog);
    for(int row=0;row<2;row++)for(int k=0;k<4;k++)
      dj_box(deck,m->sole,-.37f-row*.025f,1.268f,side*.20f+(k-1.5f)*.032f,.019f,.009f,.025f);
  }
  for(int k=0;k<3;k++)dj_box(deck,m->alloy,-.27f+k*.045f,1.273f,0,.016f,.014f,.09f);
  /* Laptop stand: two rising arms support the tray above the controller. */
  for(int side=-1;side<=1;side+=2)
    tube3(deck,cyl,m->black,(t3_vec3){.0f,1.22f,side*.12f},(t3_vec3){-.02f,1.32f,side*.12f},.012f);
  /* Laptop, keyboard and raised display facing the rider. */
  dj_box(deck,m->alloy,-.02f,1.33f,0,.23f,.018f,.34f);
  dj_box(deck,m->black,-.025f,1.341f,0,.15f,.005f,.29f);
  dj_box(deck,m->alloy,.085f,1.46f,0,.015f,.25f,.34f);
  dj_box(deck,m->black,.075f,1.46f,0,.008f,.22f,.31f);
  /* Software panes and two coloured waveform tracks. */
  for(int row=0;row<2;row++)for(int k=0;k<18;k++)
    dj_box(deck,row?violet:white,.069f,1.50f-row*.05f,(k-8.5f)*.015f,.004f,.008f+.02f*((k*7)%9)/9.f,.011f);
  for(int k=0;k<5;k++)dj_box(deck,m->alloy,.069f,1.40f+k*.012f,0,.004f,.003f,.26f);
  /* Portable power supply and visible cable runs beneath the podium. */
  dj_box(p,m->black,-.40f,.36f,0,.22f,.19f,.42f);
  for(int side=-1;side<=1;side+=2)
    tube3(p,cyl,m->rubber,(t3_vec3){-.44f,1.2f,side*.28f},(t3_vec3){-.42f,.41f,side*.27f},.006f);
  t3_release(violet);t3_release(white);
  return p;
}
