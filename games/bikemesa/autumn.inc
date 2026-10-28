/* Leaf Fall is an orchard/woodland trail. Keep the riding lanes readable;
 * landmarks, split-rail fences and leaf litter live on the verges. */
static void autumn_build(t3_object *root,const track_desc *t) {
  if(strcmp(t->theme,"autumn"))return;
  t3_geometry *box=t3_box_geometry_new(1,1,1,1,1,1);
  t3_geometry *ball=t3_sphere_geometry_new(1,12,8);
  t3_geometry *cyl=t3_cylinder_geometry_new(1,1,1,10,1,false);
  t3_material *wood=mat(TEX_WOOD,(float[3]){.46f,.32f,.20f},1,2,.6f,.95f);
  t3_material *red=mat(TEX_WOOD,(float[3]){.53f,.14f,.10f},2,2,.4f,.9f);
  t3_material *roof=mat(TEX_WOOD,(float[3]){.18f,.23f,.22f},2,2,.4f,.95f);
  t3_material *cream=t3_mesh_standard_material_new(0xe4d7b2);
  t3_material *dark=t3_mesh_standard_material_new(0x222521);
  t3_material *orange=t3_mesh_standard_material_new(0xd66c18);
  t3_material *gold=t3_mesh_standard_material_new(0xbda23b);
  cream->roughness=dark->roughness=orange->roughness=gold->roughness=1;
  /* Weathered split rails follow the bank, with gaps at discoveries. */
  for(float x=8;x<t->finish_x;x+=3.2f) {
    if(fabsf(x-t->finish_x*.54f)<7 || fabsf(x-t->finish_x*.81f)<8)continue;
    float z=-3.7f,y=bank_surface_y(x,z,0);
    prop(root,cyl,wood,x,y+.64f,z,.075f,1.35f,.075f);
    float next=bank_surface_y(x+3.2f,z,0);
    for(int j=0;j<2;j++) {
      t3_mesh *rail=prop(root,box,wood,x+1.6f,(y+next)*.5f+.43f+j*.43f,z,3.24f,.105f,.09f);
      t3_object_set_rotation(&rail->base,0,0,cw_atan2(next-y,3.2f));
    }
  }
  /* Red cider barn, a landmark with timber trim and a broad pitched roof. */
  float bx=t->finish_x*.31f,bz=-11,by=bank_surface_y(bx,bz,0);
  prop(root,box,red,bx,by+2.1f,bz,9,4.2f,5);
  prop(root,box,wood,bx,by+.12f,bz,10,.3f,6);
  for(int side=-1;side<=1;side+=2) {
    t3_mesh *panel=prop(root,box,roof,bx,by+4.5f,bz+side*1.6f,10,.20f,3.8f);
    t3_object_set_rotation(&panel->base,side*.49f,0,0);
    prop(root,box,cream,bx+side*4.42f,by+2.1f,bz+2.55f,.14f,4.2f,.12f);
    prop(root,box,dark,bx+side*2.7f,by+2.4f,bz+2.52f,1.2f,1.4f,.05f);
    prop(root,box,cream,bx+side*2.7f,by+2.4f,bz+2.58f,.07f,1.45f,.06f);
    prop(root,box,cream,bx+side*2.7f,by+2.4f,bz+2.58f,1.25f,.07f,.06f);
  }
  prop(root,box,dark,bx,by+1.5f,bz+2.52f,2.7f,3,.06f);
  prop(root,box,cream,bx,by+3.05f,bz+2.58f,3,.12f,.1f);
  for(int side=-1;side<=1;side+=2) {
    prop(root,box,wood,bx+side*.7f,by+1.45f,bz+2.57f,1.3f,2.8f,.1f);
    t3_mesh *brace=prop(root,box,cream,bx+side*.7f,by+1.45f,bz+2.66f,.10f,3,.05f);
    t3_object_set_rotation(&brace->base,0,0,side*.42f);
  }
  /* Harvest rows: lobed pumpkins, stalks and rectangular straw bales. */
  for(int i=0;i<62;i++) {
    float x=t->finish_x*.70f+(i%11-5)*1.05f,z=-5-(i/11)*1.4f;
    float y=bank_surface_y(x,z,0),r=.24f+.14f*rnd();
    for(int j=0;j<7;j++) {
      float a=j*6.2831853f/7;
      prop(root,ball,orange,x+cw_cos(a)*r*.30f,y+r*.80f,z+cw_sin(a)*r*.30f,r*.8f,r*.82f,r*.8f);
    }
    prop(root,cyl,wood,x,y+r*1.65f,z,.035f,.19f,.035f);
  }
  for(int i=0;i<7;i++) {
    float x=bx+6+(i%3)*1.25f,z=bz+1+(i/3)*1.1f,y=bank_surface_y(x,z,0);
    prop(root,box,gold,x,y+.4f,z,1.1f,.8f,.8f);
    for(int j=-1;j<=1;j+=2)prop(root,box,wood,x+j*.31f,y+.4f,z+.405f,.035f,.81f,.025f);
  }
  /* Real leaf silhouettes on the trail shoulders, batched by autumn tint. */
  const unsigned colors[]={0xc37c25,0xa64722,0xd4a744};
  for(int tint=0;tint<3;tint++) {
    mb carpet={0};
    for(int i=0;i<750;i++) {
      float x=rnd()*t->finish_x,z=-2.5f-rnd()*6;
      if(i%4==0)z=1.85f+rnd()*.65f;
      bool found;float y=track_height_at(x,&found);if(!found)continue;
      if(z < -TRACK_HALF_W-.3f)y=bank_surface_y(x,z,0);
      y+=.035f;float a=rnd()*6.2831853f,r=.09f+rnd()*.14f;
      int c=mb_v(&carpet,(t3_vec3){x,y+.025f,z},(t3_vec3){0,1,0},.5f,.5f,1);
      for(int k=0;k<10;k++) {
        float ang=a+k*6.2831853f/10,reach=r*(k%2?.46f:1);
        mb_v(&carpet,(t3_vec3){x+cw_cos(ang)*reach,y,z+cw_sin(ang)*reach},(t3_vec3){0,1,0},0,0,1);
      }
      for(int k=0;k<10;k++)mb_t(&carpet,c,c+1+(k+1)%10,c+1+k);
    }
    t3_geometry *g=mb_done(&carpet);t3_material *m=t3_mesh_standard_material_new(colors[tint]);m->roughness=1;
    add(root,g,m,false);t3_release(g);t3_release(m);
  }
  t3_release(box);t3_release(ball);t3_release(cyl);
  t3_material *materials[]={wood,red,roof,cream,dark,orange,gold};
  for(int i=0;i<7;i++)t3_release(materials[i]);
}
