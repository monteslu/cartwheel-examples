/* Compact 20-inch BMX, with a real open lattice crate on its front rack. */
static t3_object *build_bmx_frame(const mats *m,t3_geometry *cyl,bike_fit fit) {
  t3_object *f=t3_group_new();
  t3_vec2 seat={-.17f,.40f},head={.43f,.52f},lower={.48f,.29f};
  tube(f,cyl,m->paint,BB,seat,.023f,0);
  tube(f,cyl,m->paint,seat,head,.023f,0);
  tube(f,cyl,m->paint,BB,lower,.026f,0);
  tube(f,cyl,m->paint,head,lower,.028f,0);
  for(int side=-1;side<=1;side+=2) {
    tube(f,cyl,m->paint,BB,(t3_vec2){fit.rear_axle.x,fit.rear_axle.y},.014f,side*.065f);
    tube(f,cyl,m->paint,seat,(t3_vec2){fit.rear_axle.x,fit.rear_axle.y},.014f,side*.065f);
    tube(f,cyl,m->alloy,lower,(t3_vec2){fit.front_axle.x,fit.front_axle.y},.018f,side*.065f);
    tube3(f,cyl,m->alloy,(t3_vec3){.41f,.67f,0},(t3_vec3){.40f,.92f,side*.20f},.013f);
    tube3(f,cyl,m->alloy,(t3_vec3){.40f,.92f,side*.20f},(t3_vec3){.37f,.96f,side*.32f},.013f);
    tube3(f,cyl,m->black,(t3_vec3){.395f,.95f,side*.23f},(t3_vec3){.37f,.96f,side*.32f},.019f);
    /* Frame-mounted cantilever rack, triangulated from the head tube.
     * No stay runs to the wheel axle or across the tire. */
    tube3(f,cyl,m->alloy,(t3_vec3){.44f,.48f,side*.035f},(t3_vec3){.91f,.458f,side*.14f},.011f);
    tube3(f,cyl,m->alloy,(t3_vec3){.475f,.315f,side*.035f},(t3_vec3){.85f,.458f,side*.14f},.010f);
  }
  tube3(f,cyl,m->black,(t3_vec3){.44f,.48f,-.04f},(t3_vec3){.44f,.48f,.04f},.035f);
  tube3(f,cyl,m->black,(t3_vec3){.475f,.315f,-.04f},(t3_vec3){.475f,.315f,.04f},.035f);
  tube3(f,cyl,m->alloy,(t3_vec3){.40f,.83f,-.13f},(t3_vec3){.40f,.83f,.13f},.010f);
  tube(f,cyl,m->alloy,head,(t3_vec2){.41f,.67f},.019f,0);
  tube(f,cyl,m->alloy,seat,(t3_vec2){fit.saddle.x,fit.saddle.y-.025f},.014f,0);
  t3_geometry *sphere=t3_sphere_geometry_new(1,20,12);
  t3_mesh *sad=mesh(f,sphere,m->saddle);sad->base.position=(t3_vec3){fit.saddle.x,fit.saddle.y,0};sad->base.scale=(t3_vec3){.14f,.035f,.08f};t3_release(sphere);
  tube(f,cyl,m->black,(t3_vec2){-.5f,-.035f},(t3_vec2){0,.12f},.005f,.095f);
  tube(f,cyl,m->black,(t3_vec2){-.5f,-.125f},(t3_vec2){0,-.12f},.005f,.095f);
  /* White polymer lattice: open holes, doubled rims and stout corners. */
  t3_material *plastic=t3_mesh_standard_material_new(0xffffff);
  plastic->color=lin(.82f,.84f,.81f);
  plastic->roughness=.8f;
  /* Outside dimensions 13 x 13 x 11 inches (Farmplast standard crate). */
  const float lo=.76f-.1591f,hi=.76f+.1591f,bottom=.476f,top=.7434f,zmax=.1591f;
  for(int side=-1;side<=1;side+=2) {
    float z=side*zmax,x=side<0?lo:hi;
    for(int j=0;j<=6;j++) {
      float xx=lo+(hi-lo)*j/6, zz=-zmax+2*zmax*j/6;
      tube3(f,cyl,plastic,(t3_vec3){xx,bottom,z},(t3_vec3){xx,top,z},.003f);
      tube3(f,cyl,plastic,(t3_vec3){x,bottom,zz},(t3_vec3){x,top,zz},.003f);
    }
    for(int j=0;j<=5;j++) {
      float y=bottom+(top-bottom)*j/5,r=(j==0||j==5)?.006f:.003f;
      tube3(f,cyl,plastic,(t3_vec3){lo,y,z},(t3_vec3){hi,y,z},r);
      tube3(f,cyl,plastic,(t3_vec3){x,y,-zmax},(t3_vec3){x,y,zmax},r);
    }
  }
  t3_geometry *floor=t3_box_geometry_new(.3302f,.012f,.3302f,1,1,1);
  t3_mesh *base=mesh(f,floor,plastic);base->base.position=(t3_vec3){.76f,.476f,0};t3_release(floor);t3_release(plastic);
  return f;
}

static void alien_blob(t3_object *p,t3_geometry *sphere,t3_material *mat,
                       t3_vec3 at,t3_vec3 scale) {
  t3_mesh *me=mesh(p,sphere,mat);me->base.position=at;me->base.scale=scale;
}
static void build_bmx_alien(t3_geometry *cyl) {
  t3_material *skin=t3_mesh_standard_material_new(0xffffff);
  skin->color=lin(.38f,.24f,.15f);skin->roughness=.92f;
  t3_material *cloth=t3_mesh_standard_material_new(0xffffff);
  cloth->color=lin(.88f,.89f,.83f);cloth->roughness=1;
  t3_material *eye=t3_mesh_standard_material_new(0xffffff);
  eye->color=lin(.035f,.055f,.065f);eye->roughness=.22f;
  t3_geometry *sphere=t3_sphere_geometry_new(1,24,16);
  for(int i=0;i<ALIEN_PARTS;i++) {
    M.alien[i]=t3_group_new();t3_object_add(M.root,M.alien[i]);t3_release(M.alien[i]);
    t3_object *part=M.alien[i];
    /* Scale mesh geometry, not the animated bone, so ejected parts retain size. */
    t3_object *g=t3_group_new();t3_object_add(part,g);t3_release(g);
    g->scale=(t3_vec3){.7f,.7f,.7f};
    if(i==0) {
      alien_blob(g,sphere,cloth,(t3_vec3){0,0,0},(t3_vec3){.17f,.24f,.17f});
      for(int k=0;k<9;k++) {
        float a=k*6.2831853f/9;
        tube3(g,cyl,cloth,(t3_vec3){.10f*cosf(a),.12f,.10f*sinf(a)},
          (t3_vec3){.16f*cosf(a),-.18f,.16f*sinf(a)},.018f);
      }
    } else if(i==1) {
      /* Hood wraps the back, leaving the wrinkled brown face exposed. */
      alien_blob(g,sphere,cloth,(t3_vec3){-.055f,.012f,0},(t3_vec3){.17f,.19f,.19f});
      alien_blob(g,sphere,skin,(t3_vec3){.045f,0,0},(t3_vec3){.16f,.145f,.15f});
      for(int side=-1;side<=1;side+=2) {
        alien_blob(g,sphere,skin,(t3_vec3){.15f,.035f,side*.092f},(t3_vec3){.065f,.061f,.062f});
        alien_blob(g,sphere,eye,(t3_vec3){.195f,.04f,side*.097f},(t3_vec3){.023f,.036f,.038f});
        alien_blob(g,sphere,cloth,(t3_vec3){.213f,.052f,side*.103f},(t3_vec3){.006f,.009f,.009f});
        for(int j=0;j<4;j++)tube3(g,cyl,skin,
          (t3_vec3){.17f,.095f+j*.009f,side*.025f},(t3_vec3){.13f,.09f+j*.009f,side*.12f},.004f);
        for(int j=0;j<3;j++)tube3(g,cyl,skin,
          (t3_vec3){.177f,-.065f-j*.013f,side*.018f},(t3_vec3){.14f,-.06f-j*.013f,side*.10f},.004f);
      }
      alien_blob(g,sphere,skin,(t3_vec3){.19f,-.025f,0},(t3_vec3){.03f,.035f,.046f});
      tube3(g,cyl,eye,(t3_vec3){.176f,-.081f,-.04f},(t3_vec3){.176f,-.081f,.04f},.004f);
    } else {
      alien_blob(g,sphere,skin,(t3_vec3){0,0,0},(t3_vec3){.035f,.12f,.037f});
      if(i==3||i==5) {
        alien_blob(g,sphere,skin,(t3_vec3){0,-.12f,0},(t3_vec3){.035f,.042f,.05f});
        for(int j=0;j<3;j++)tube3(g,cyl,skin,(t3_vec3){.01f,-.13f,(j-1)*.025f},
          (t3_vec3){.025f,-.205f,(j-1)*.035f},.009f);
      } else if(i>=6)alien_blob(g,sphere,skin,(t3_vec3){.025f,-.10f,0},(t3_vec3){.075f,.035f,.046f});
    }
  }
  t3_release(sphere);t3_release(skin);t3_release(cloth);t3_release(eye);
}
