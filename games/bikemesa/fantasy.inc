/* The enchanted forest village sits beyond the opening crossing. Rigid pieces
 * merge with the scenery; only the two crossing deer stay as small groups. */
static t3_object *forest_deer[DEER_CROSSINGS];
static t3_object *forest_legs[DEER_CROSSINGS][4];

static void forest_cabin(t3_object *root,const whimsy_parts *p,float x,float z,float scale) {
  float y=bank_surface_y(x,z,1);
  t3_object *g=t3_group_new();g->position=(t3_vec3){x,y,z};g->scale=(t3_vec3){scale,scale,scale};
  t3_object_add(root,g);
  prop(g,p->box,p->brown,0,-.45f,0,5.7f,1.05f,4.5f);
  prop(g,p->box,p->brown,0,1.55f,0,5.5f,3.1f,4.3f);
  prop(g,p->box,p->cream,0,1.7f,2.18f,5.05f,2.5f,.08f);
  for(int s=-1;s<=1;s+=2) {
    t3_mesh *roof=prop(g,p->box,p->green,0,3.55f,s*1.23f,6.6f,.22f,3.05f);
    t3_object_set_rotation(&roof->base,s*.52f,0,0);
    prop(g,p->cyl,p->brown,s*2.65f,1.6f,2.27f,.18f,3.2f,.18f);
  }
  prop(g,p->box,p->black,0,1.05f,2.28f,1.1f,2.1f,.14f);
  prop(g,p->box,p->gold,-1.8f,1.9f,2.29f,.9f,.95f,.13f);
  prop(g,p->box,p->gold,1.8f,1.9f,2.29f,.9f,.95f,.13f);
  prop(g,p->cyl,p->black,1.0f,4.3f,-.9f,.37f,2.0f,.37f);
  t3_release(g);
}

static void forest_person(t3_object *root,const whimsy_parts *p,t3_material *cloth[3],float x,float z,float size,int kind) {
  float y=bank_surface_y(x,z,1);
  t3_object *g=t3_group_new();g->position=(t3_vec3){x,y,z};g->scale=(t3_vec3){size,size,size};
  t3_object_add(root,g);
  t3_material *coat=cloth[kind];
  prop(g,p->cone,coat,0,.65f,0,.53f,1.3f,.48f);
  prop(g,p->ball,p->cream,0,1.55f,0,.29f,.31f,.28f);
  prop(g,p->ball,p->black,-.11f,1.59f,.27f,.035f,.04f,.02f);
  prop(g,p->ball,p->black,.11f,1.59f,.27f,.035f,.04f,.02f);
  if(kind==0) { /* Azure robe and star clasp, an original forest astronomer. */
    prop(g,p->cone,coat,0,1.97f,0,.45f,.79f,.44f);
    prop(g,p->ball,p->gold,0,1.32f,.38f,.11f,.11f,.07f);
    prop(g,p->cyl,p->brown,.47f,.92f,.15f,.055f,1.8f,.055f);
    prop(g,p->ball,p->lamp,.47f,1.87f,.15f,.13f,.13f,.13f);
  } else {
    prop(g,p->cone,coat,0,1.89f,0,.48f,.45f,.44f);
    if(kind==1) for(int s=-1;s<=1;s+=2)
      prop(g,p->cone,p->cream,s*.31f,1.6f,0,.19f,.39f,.15f);
  }
  t3_release(g);
}

static void fantasy_camp(t3_object *root,const whimsy_parts *p) {
  t3_material *cloth[3]={
    t3_mesh_standard_material_new(0x2869ac),
    t3_mesh_standard_material_new(0x647d42),
    t3_mesh_standard_material_new(0x8a654b)
  };
  for(int i=0;i<3;i++){cloth[i]->roughness=1.0f;cloth[i]->metalness=0;}
  forest_cabin(root,p,192,-13,1.12f);
  forest_cabin(root,p,224,-20,.85f);
  forest_cabin(root,p,412,-15,1.04f);
  float x=208,z=-12,y=bank_surface_y(x,z,1);
  for(int i=0;i<7;i++) {
    float a=i*.897598f;
    float lx=x+cw_cos(a)*.65f,lz=z+cw_sin(a)*.65f;
    t3_mesh *log=prop(root,p->cyl,p->brown,lx,bank_surface_y(lx,lz,1)+.12f,lz,.11f,.8f,.11f);
    t3_object_set_rotation(&log->base,.9f*cw_sin(a),0,.9f*cw_cos(a));
  }
  prop(root,p->cone,p->red,x,y+.45f,z,.31f,.9f,.31f);
  t3_mesh *flame=prop(root,p->cone,p->lamp,x,y+.65f,z,.18f,.68f,.18f);
  whimsy_bob(flame,.5f);
  forest_person(root,p,cloth,205,-12.5f,1.35f,0);
  forest_person(root,p,cloth,211,-12.8f,.67f,1);
  forest_person(root,p,cloth,209,-15.5f,.73f,2);
  forest_person(root,p,cloth,229,-16.5f,.95f,1);
  for(int i=0;i<3;i++)t3_release(cloth[i]);
}

static void forest_deer_build(t3_object *root,const whimsy_parts *p) {
  t3_material *fur=t3_mesh_standard_material_new(0x795036);
  t3_material *pale=t3_mesh_standard_material_new(0xc5aa7c);
  fur->roughness=pale->roughness=1.0f;
  fur->metalness=pale->metalness=0;
  for(int i=0;i<DEER_CROSSINGS;i++) {
    t3_object *g=t3_group_new();t3_object_add(root,g);forest_deer[i]=g;
    prop(g,p->ball,fur,0,1.26f,0,.43f,.48f,1.04f);
    prop(g,p->ball,pale,0,1.05f,.22f,.28f,.27f,.59f);
    prop(g,p->ball,fur,0,1.62f,.7f,.25f,.51f,.29f);
    prop(g,p->ball,fur,0,1.93f,.92f,.29f,.24f,.35f);
    prop(g,p->ball,pale,0,1.82f,1.22f,.21f,.12f,.23f);
    prop(g,p->ball,p->black,0,1.87f,1.42f,.095f,.075f,.06f);
    for(int s=-1;s<=1;s+=2) {
      prop(g,p->cone,fur,s*.27f,2.18f,.78f,.13f,.38f,.17f);
      prop(g,p->ball,p->black,s*.19f,2.0f,1.18f,.045f,.05f,.045f);
      prop(g,p->cyl,pale,s*.19f,2.34f,.84f,.035f,.48f,.035f);
      prop(g,p->cyl,pale,s*.34f,2.52f,.84f,.03f,.29f,.03f);
    }
    prop(g,p->ball,pale,0,1.33f,-.89f,.2f,.21f,.23f);
    for(int k=0;k<4;k++) {
      t3_object *leg=t3_group_new();leg->position=(t3_vec3){k%2?.32f:-.32f,1.02f,k/2?.62f:-.62f};
      t3_object_add(g,leg);forest_legs[i][k]=leg;
      prop(leg,p->cyl,fur,0,-.42f,0,.075f,.84f,.08f);
      prop(leg,p->box,p->black,0,-.84f,.08f,.19f,.14f,.27f);
      t3_release(leg);
    }
    g->visible=false;t3_release(g);
  }
  t3_release(fur);t3_release(pale);
}

static void forest_deer_frame(float alpha) {
  if(!forest_deer[0])return;
  t3_vec3 poses[DEER_CROSSINGS];int active=bike_deer_poses(alpha,poses);
  for(int i=0;i<DEER_CROSSINGS;i++) {
    t3_object *g=forest_deer[i];g->visible=(active&(1<<i))!=0;
    if(!g->visible)continue;
    float gait=whimsy_time*12+i*1.7f;
    float gy=poses[i].z<-TRACK_HALF_W-.3f?bank_surface_y(poses[i].x,poses[i].z,1):poses[i].y;
    g->position=(t3_vec3){poses[i].x,gy+.07f*cw_sin(gait*2),poses[i].z};
    for(int k=0;k<4;k++)t3_object_set_rotation(forest_legs[i][k],.28f*cw_sin(gait+(k==0||k==3?0:3.1415927f)),0,0);
  }
}
