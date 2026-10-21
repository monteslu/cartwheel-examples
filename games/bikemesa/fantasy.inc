/* The enchanted forest village sits beyond the opening crossing. Rigid pieces
 * merge with the scenery; only the two crossing deer stay as small groups. */
static t3_object *forest_deer[DEER_CROSSINGS];
static t3_object *forest_legs[DEER_CROSSINGS][4];

static void forest_cabin(t3_object *root,const whimsy_parts *p,float x,float z,float scale) {
  float y=bank_surface_y(x,z,0);
  t3_object *g=t3_group_new();g->position=(t3_vec3){x,y,z};g->scale=(t3_vec3){scale,scale,scale};
  t3_object_add(root,g);
  prop(g,p->box,p->brown,0,-.45f,0,5.7f,1.05f,4.5f);
  for(int sx=-1;sx<=1;sx+=2)for(int sz=-1;sz<=1;sz+=2) {
    float ground=(bank_surface_y(x+sx*2.45f*scale,z+sz*1.95f*scale,0)-y)/scale;
    float bottom=fminf(ground-.18f,-1.0f),top=-.48f;
    prop(g,p->box,p->brown,sx*2.45f,(bottom+top)*.5f,sz*1.95f,
      .44f,top-bottom,.44f);
  }
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

/* A single smooth, pleated garment is reused by all four villagers. Its
 * silhouette and shallow folds do the work that the old stretched cones did
 * poorly; fabric normal/roughness maps add detail without extra draw calls. */
static t3_geometry *forest_robe_geometry(void) {
  enum { SECTORS=32, RINGS=9 };
  mb b={0};
  for(int k=0;k<=RINGS;k++) {
    float t=(float)k/RINGS,y=.05f+1.36f*t;
    float radius=.55f-.22f*t+.11f*cw_sin(t*3.1415927f);
    for(int i=0;i<=SECTORS;i++) {
      float a=i*6.2831853f/SECTORS;
      float fold=(.033f+.028f*(1-t))*cw_cos(a*11+2.2f*t)
        +.016f*cw_sin(a*5-4.0f*t);
      float r=radius+fold;
      mb_v(&b,(t3_vec3){r*cw_cos(a),y,r*cw_sin(a)},
        (t3_vec3){cw_cos(a),.15f,cw_sin(a)},(float)i/SECTORS,2*t,1);
    }
  }
  for(int k=0;k<RINGS;k++)for(int i=0;i<SECTORS;i++) {
    int a=k*(SECTORS+1)+i,c=a+SECTORS+1;
    mb_t(&b,a,c,a+1);mb_t(&b,a+1,c,c+1);
  }
  t3_geometry *g=mb_done(&b);
  t3_geometry_compute_vertex_normals(g);
  return g;
}

static void forest_person(t3_object *root,const whimsy_parts *p,t3_geometry *robe,t3_geometry *smooth,
                          t3_material *cloth[3],t3_material *trim[3],t3_material *skin,
                          t3_material *hair,t3_material *leather,float x,float z,float size,int kind) {
  float y=bank_surface_y(x,z,0);
  t3_object *g=t3_group_new();g->position=(t3_vec3){x,y,z};g->scale=(t3_vec3){size,size,size};
  t3_object_add(root,g);
  t3_material *coat=cloth[kind],*edge=trim[kind];
  prop(g,robe,coat,0,0,0,1,1,1);
  /* Layered front panels and a soft shoulder cape break the cone silhouette. */
  for(int s=-1;s<=1;s+=2) {
    t3_mesh *panel=prop(g,p->box,edge,s*.23f,.79f,.35f,.22f,1.02f,.045f);
    t3_object_set_rotation(&panel->base,0,s*.13f,s*.06f);
    prop(g,smooth,coat,s*.20f,1.33f,.02f,.28f,.18f,.34f);
  }
  t3_mesh *belt=prop(g,p->ring,edge,0,.80f,0,.37f,.37f,.37f);
  t3_object_set_rotation(&belt->base,1.5707963f,0,0);
  for(int s=-1;s<=1;s+=2) {
    float bx=s*.22f,bz=.16f;
    float by=(bank_surface_y(x+bx*size,z+bz*size,0)-y)/size;
    prop(g,smooth,leather,bx,by+.13f,bz,.14f,.15f,.24f);
    prop(g,p->cyl,edge,bx,by+.36f,.02f,.13f,.48f,.15f);
    t3_mesh *sleeve=prop(g,p->cyl,coat,s*.48f,1.10f,.03f,.18f,.64f,.19f);
    t3_object_set_rotation(&sleeve->base,0,0,s*.23f);
    prop(g,smooth,skin,s*.53f,.79f,.09f,.13f,.14f,.12f);
  }
  prop(g,smooth,skin,0,1.65f,.08f,.25f,.29f,.25f);
  prop(g,smooth,skin,0,1.60f,.32f,.11f,.10f,.11f); /* nose */
  for(int s=-1;s<=1;s+=2) {
    prop(g,smooth,skin,s*.24f,1.65f,.08f,.075f,.11f,.07f);
    prop(g,smooth,hair,s*.105f,1.70f,.295f,.025f,.03f,.018f);
  }
  prop(g,smooth,coat,0,1.86f,-.09f,.31f,.25f,.28f); /* soft hood */
  prop(g,smooth,edge,0,1.91f,.10f,.31f,.10f,.25f);
  if(kind==0) { /* A blue forest astronomer with a hood and a wool beard. */
    prop(g,smooth,hair,0,1.44f,.27f,.20f,.17f,.15f);
    prop(g,smooth,edge,0,1.31f,.40f,.085f,.09f,.055f);
    prop(g,p->cyl,leather,.53f,.93f,.16f,.045f,1.85f,.045f);
    prop(g,smooth,edge,.53f,1.87f,.16f,.10f,.12f,.10f);
  } else {
    prop(g,smooth,leather,0,1.45f,-.23f,.31f,.33f,.12f); /* travel pack */
    prop(g,p->box,edge,0,1.00f,.39f,.31f,.31f,.08f); /* satchel */
  }
  /* These villagers never animate. Flatten them into the scenery root so
   * merge_still can batch each fabric and shared skin material by chunk. */
  while(g->child_count) {
    t3_object *o=g->children[0];
    t3_vec3 at=o->position,sc=o->scale;
    t3_object_add(root,o);
    o->position=(t3_vec3){x+at.x*size,y+at.y*size,z+at.z*size};
    o->scale=(t3_vec3){sc.x*size,sc.y*size,sc.z*size};
  }
  t3_object_remove(root,g);
  t3_release(g);
}

static void forest_flames(t3_object *root,float x,float y,float z) {
  t3_material *outer=glow(1.0f,.27f,.025f,3.2f);
  t3_material *inner=glow(1.0f,.77f,.18f,5.0f);
  for(int layer=0;layer<2;layer++) {
    mb b={0};
    for(int i=0;i<5;i++) {
      float a=i*2.39996f+.3f,ca=cw_cos(a),sa=cw_sin(a);
      float r=layer?.17f:.32f,h=(layer?.58f:1.03f)*(i%2?.82f:1.0f);
      float ox=cw_cos(a*1.7f)*r*.5f,oz=cw_sin(a*1.7f)*r*.5f;
      t3_vec3 n={sa,0,-ca};
      int l=mb_v(&b,(t3_vec3){x+ox-ca*r,y+.08f,z+oz-sa*r},n,0,0,1);
      int rr=mb_v(&b,(t3_vec3){x+ox+ca*r,y+.08f,z+oz+sa*r},n,1,0,1);
      int m=mb_v(&b,(t3_vec3){x+ox+ca*r*.15f,y+h*.52f,z+oz+sa*r*.15f},n,.55f,.5f,1);
      int tip=mb_v(&b,(t3_vec3){x+ox+ca*r*.52f,y+h,z+oz+sa*r*.52f},n,.76f,1,1);
      mb_t(&b,l,rr,m);mb_t(&b,l,m,tip);
      mb_t(&b,m,rr,tip);mb_t(&b,m,rr,l);mb_t(&b,tip,m,l);mb_t(&b,tip,rr,m);
    }
    t3_geometry *geo=mb_done(&b);
    add(root,geo,layer?inner:outer,false);
    t3_release(geo);
  }
  t3_release(outer);t3_release(inner);
}

static void fantasy_camp(t3_object *root,const whimsy_parts *p) {
  t3_material *cloth[3]={
    mat(TEX_FABRIC,(float[3]){.16f,.27f,.48f},2,3,.65f,1),
    mat(TEX_FABRIC,(float[3]){.37f,.45f,.26f},2,3,.65f,1),
    mat(TEX_FABRIC,(float[3]){.49f,.34f,.24f},2,3,.65f,1)
  };
  t3_material *trim[3]={
    mat(TEX_COTTON,(float[3]){.31f,.43f,.57f},2,2,.5f,1),
    mat(TEX_COTTON,(float[3]){.63f,.59f,.38f},2,2,.5f,1),
    mat(TEX_COTTON,(float[3]){.68f,.51f,.34f},2,2,.5f,1)
  };
  t3_material *skin=t3_mesh_standard_material_new(0xb98264);
  t3_material *hair=t3_mesh_standard_material_new(0x33251d);
  t3_material *leather=mat(TEX_FABRIC,(float[3]){.19f,.11f,.065f},2,2,.3f,1);
  skin->roughness=hair->roughness=1;
  skin->metalness=hair->metalness=leather->metalness=0;
  t3_geometry *robe=forest_robe_geometry();
  t3_geometry *smooth=t3_sphere_geometry_new(1,24,16);
  forest_cabin(root,p,192,-13,1.12f);
  forest_cabin(root,p,224,-20,.85f);
  forest_cabin(root,p,412,-15,1.04f);
  float x=208,z=-11,y=bank_surface_y(x,z,0);
  for(int i=0;i<12;i++) {
    float a=i*.523599f,lx=x+cw_cos(a)*.82f,lz=z+cw_sin(a)*.82f;
    prop(root,p->ball,i%3?p->brown:p->black,lx,bank_surface_y(lx,lz,0)+.10f,lz,.21f,.14f,.17f);
  }
  for(int i=0;i<4;i++) {
    float a=i*.785398f;
    t3_mesh *log=prop(root,p->cyl,i%2?p->brown:p->black,x,y+.17f,z,.12f,1.35f,.12f);
    t3_object_set_rotation(&log->base,1.5707963f,.0f,a);
  }
  prop(root,p->ball,p->black,x,y+.06f,z,.62f,.12f,.58f);
  forest_flames(root,x,y,z);
  for(int i=0;i<5;i++) {
    float a=i*2.39996f;
    t3_mesh *ember=prop(root,p->ball,p->lamp,x+cw_cos(a)*.28f,y+.37f+(i%3)*.2f,z+cw_sin(a)*.21f,.045f,.05f,.045f);
    whimsy_bob(ember,a);
  }
  forest_person(root,p,robe,smooth,cloth,trim,skin,hair,leather,205,-11.4f,1.08f,0);
  forest_person(root,p,robe,smooth,cloth,trim,skin,hair,leather,211,-11.7f,.67f,1);
  forest_person(root,p,robe,smooth,cloth,trim,skin,hair,leather,209,-14.4f,.73f,2);
  forest_person(root,p,robe,smooth,cloth,trim,skin,hair,leather,229,-16.5f,.95f,1);
  t3_release(robe);t3_release(smooth);
  for(int i=0;i<3;i++){t3_release(cloth[i]);t3_release(trim[i]);}
  t3_release(skin);t3_release(hair);t3_release(leather);
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

void view_deer_poses(const deer_pose poses[DEER_CROSSINGS]) {
  if(!forest_deer[0])return;
  for(int i=0;i<DEER_CROSSINGS;i++) {
    t3_object *g=forest_deer[i];g->visible=poses[i].visible;
    if(!g->visible)continue;
    float gait=poses[i].gait+i*1.7f;
    float gy=poses[i].p.z<-TRACK_HALF_W-.3f?bank_surface_y(poses[i].p.x,poses[i].p.z,0):poses[i].p.y;
    g->position=(t3_vec3){poses[i].p.x,gy+(1-poses[i].wreck)*.07f*cw_sin(gait*2),poses[i].p.z};
    t3_object_set_rotation(g,0,0,poses[i].roll);
    for(int k=0;k<4;k++)t3_object_set_rotation(forest_legs[i][k],
      (1-poses[i].wreck)*.28f*cw_sin(gait+(k==0||k==3?0:3.1415927f)) + poses[i].wreck*(k<2?-.65f:.8f),0,0);
    t3_object_update_world_matrix(g,true,true);
  }
}
