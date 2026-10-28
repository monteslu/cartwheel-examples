/* A quiet residential neighborhood built around three moonlit flights. */
static t3_texture *burbs_moon_texture(void) {
  size_t n=0;void *bytes=cw_asset_load("sky/moon-lroc.jpg",&n);
  t3_texture *tx=bytes?t3_texture_load_memory(bytes,n):NULL;free(bytes);
  if(tx){tx->color_space=T3_SRGB_COLOR_SPACE;tx->generate_mipmaps=true;tx->min_filter=T3_LINEAR_MIPMAP_LINEAR;}
  return tx;
}
static t3_texture *burbs_moon_halo(void) {
  enum{N=128};uint8_t px[N*N*4];
  for(int y=0;y<N;y++)for(int x=0;x<N;x++){
    float u=2.f*x/(N-1)-1,v=2.f*y/(N-1)-1,r=sqrtf(u*u+v*v);
    float a=expf(-r*r*5)*(1-cw_smoothstep(.8f,1,r));
    int k=(y*N+x)*4;px[k]=170;px[k+1]=199;px[k+2]=255;px[k+3]=(uint8_t)(a*100);
  }
  t3_texture *tx=t3_texture_new(N,N,px);tx->color_space=T3_SRGB_COLOR_SPACE;return tx;
}
static t3_geometry *burbs_foliage(void) {
  mb b={0};
  /* Opaque irregular inner mass supports the leafy silhouette. */
  for(int r=0;r<9;r++)for(int c=0;c<17;c++) {
    float th=r*3.1415927f/8,a=c*6.2831853f/16,k=.71f+.06f*sinf(a*5+th*7);
    t3_vec3 n={sinf(th)*cosf(a),cosf(th),sinf(th)*sinf(a)};
    mb_v(&b,(t3_vec3){n.x*k,n.y*k*.82f,n.z*k},n,c/16.f,r/8.f,.55f);
  }
  for(int r=0;r<8;r++)for(int c=0;c<16;c++){int a=r*17+c;mb_t(&b,a,a+1,a+17);mb_t(&b,a+1,a+18,a+17);}
  for(int i=0;i<110;i++) {
    float a=rnd()*6.2831853f,yy=rnd()*2-1,rr=sqrtf(1-yy*yy),sz=.16f+.07f*rnd(),tilt=rnd()*2.4f-1.2f;
    t3_vec3 c={rr*cosf(a),yy*.82f,rr*sinf(a)},n={-sinf(a)*cosf(tilt),-sinf(tilt),cosf(a)*cosf(tilt)};
    int v=mb_v(&b,(t3_vec3){c.x+n.x*.035f,c.y+n.y*.035f,c.z+n.z*.035f},n,.5f,.5f,.8f+.2f*rnd());
    for(int j=0;j<6;j++){float q=j*6.2831853f/6;mb_v(&b,(t3_vec3){c.x+cosf(a)*cosf(q)*sz-sinf(a)*sinf(tilt)*sinf(q)*sz*.7f,c.y+cosf(tilt)*sinf(q)*sz*.7f,c.z+sinf(a)*cosf(q)*sz+cosf(a)*sinf(tilt)*sinf(q)*sz*.7f},n,.5f+cosf(q)*.5f,.5f+sinf(q)*.5f,.8f+.2f*rnd());}
    for(int j=0;j<6;j++){mb_t(&b,v,v+1+j,v+1+(j+1)%6);mb_t(&b,v,v+1+(j+1)%6,v+1+j);}
  }
  return mb_done(&b);
}
static void burbs_tree(t3_object *root,t3_geometry *ball,t3_geometry *cyl,t3_material *bark,t3_material **leaf,float x,float z,bool elm) {
  float y=bank_surface_y(x,z,4),h=elm?8.5f:5.3f;
  prop(root,cyl,bark,x,y+h*.30f,z,elm?.17f:.28f,h*.6f,elm?.17f:.28f);
  for(int j=0;j<7;j++) {
    float a=j*6.2831853f/7,spread=elm?2.3f:3.3f;
    t3_vec3 start={x,y+h*.32f,z},end={x+cosf(a)*spread,y+h*(elm?.82f:.68f),z+sinf(a)*spread};
    liberty_beam(root,cyl,bark,start,end,elm?.085f:.13f);
    for(int k=0;k<3;k++) {
      float q=(k-1)*.65f;
      prop(root,ball,leaf[(j+k)%3],end.x+q*cosf(a),end.y+q*.4f,end.z+q*sinf(a),elm?1.4f:1.9f,elm?1.8f:1.2f,elm?1.3f:1.65f);
    }
  }
  prop(root,ball,leaf[1],x,y+h*.85f,z,elm?2.0f:2.8f,elm?1.6f:1.4f,2.1f);
}
static void nightburbs_build(cw_node parent,t3_object *root,const track_desc *t) {
  if(strcmp(t->theme,"nightburbs"))return;
  t3_geometry *foliage=burbs_foliage();
  t3_geometry *sky=t3_plane_geometry_new(30000,18000,1,1);
  float skycol[]={.004f,.008f,.024f,.004f,.008f,.024f,.025f,.045f,.085f,.025f,.045f,.085f};
  set_attr(sky,T3_ATTR_COLOR,t3_attribute_new(T3_FLOAT32,skycol,4,3));
  t3_material *skym=t3_mesh_basic_material_new(0xffffff);skym->vertex_colors=true;skym->fog=false;
  t3_mesh *sk=add(root,sky,skym,false);sk->base.position=(t3_vec3){150,0,-12000};t3_release(sky);t3_release(skym);
  t3_geometry *box=t3_box_geometry_new(1,1,1,1,1,1),*ball=t3_sphere_geometry_new(1,16,10),*cyl=t3_cylinder_geometry_new(1,1,1,10,1,false);
  t3_material *trim=stunt_color(.65f,.69f,.72f,0),*roof=mat(TEX_ASPHALT,(float[3]){.14f,.19f,.26f},3,3,.3f,.85f);
  t3_material *siding[3]={stunt_color(.42f,.49f,.57f,0),stunt_color(.46f,.38f,.30f,0),stunt_color(.29f,.42f,.40f,0)};
  t3_material *warm=stunt_color(1,.66f,.27f,2.3f),*dark=stunt_color(.025f,.045f,.075f,0),*metal=stunt_color(.13f,.18f,.23f,0);
  t3_material *concrete=mat(TEX_CONCRETE,(float[3]){.40f,.44f,.49f},1,1,.25f,.9f),*bark=mat(TEX_WOOD,(float[3]){.19f,.16f,.13f},1,2,.4f,1);
  t3_material *leaves[3]={stunt_color(.10f,.20f,.17f,0),stunt_color(.15f,.27f,.22f,0),stunt_color(.20f,.31f,.25f,0)};
  for(int i=0;i<3;i++)leaves[i]->roughness=1;
  /* Front lawns cover the cutaway cliff; the riding road stays a separate slab. */
  mb lawn={0};
  for(float x=-30;x<t->length+40;x+=2) {
    for(int z=0;z<2;z++){float zz=z?32:2.45f,y=ground_at(x)-.18f-(z?1.1f:0);mb_v(&lawn,(t3_vec3){x,y,zz},(t3_vec3){0,1,0},x/6,zz/6,1);}
    if(lawn.nv>2){int a=lawn.nv-4;mb_t(&lawn,a,a+1,a+2);mb_t(&lawn,a+1,a+3,a+2);}
  }
  t3_geometry *lg=mb_done(&lawn);t3_material *lm=mat(TEX_GRASS,(float[3]){.12f,.24f,.19f},1,1,.25f,1);add(root,lg,lm,true);t3_release(lg);t3_release(lm);
  /* Sidewalk slabs, curb and broken center lines make this a street. */
  for(float x=-18;x<t->length;x+=2) {
    bool on=false;float y=track_height_at(x,&on);if(!on)continue;
    prop(root,box,concrete,x,y-.03f,-2.9f,1.97f,.16f,1.1f);
    prop(root,box,trim,x,y+.025f,-2.32f,1.98f,.15f,.13f);
    if(((int)x/2)%3==0)prop(root,box,trim,x,y+.025f,0,1.4f,.02f,.07f);
  }
  /* Offset rows of detached homes: clapboards, garages, warm divided windows. */
  for(int row=0;row<2;row++)for(float x=-12+row*9;x<t->length+30;x+=17+row*5) {
    float z=-10-row*15,y=bank_surface_y(x,z,4),h=3.3f+(row?1.1f:0),w=8;
    t3_material *wall=siding[(int)(rnd()*3)];
    prop(root,box,concrete,x,y+.1f,z,9,.35f,6.8f);
    prop(root,box,wall,x,y+h*.5f,z,w,h,5.8f);
    for(int side=-1;side<=1;side+=2){t3_mesh *r=prop(root,box,roof,x,y+h+.72f,z+side*1.6f,9,.18f,3.65f);t3_object_set_rotation(&r->base,side*.43f,0,0);}
    mb gable={0};for(int side=-1;side<=1;side+=2){int v=mb_v(&gable,(t3_vec3){x+side*4,y+h,z-2.9f},(t3_vec3){side,0,0},0,0,1);mb_v(&gable,(t3_vec3){x+side*4,y+h,z+2.9f},(t3_vec3){side,0,0},1,0,1);mb_v(&gable,(t3_vec3){x+side*4,y+h+1.4f,z},(t3_vec3){side,0,0},.5f,1,1);if(side>0)mb_t(&gable,v,v+2,v+1);else mb_t(&gable,v,v+1,v+2);}t3_geometry *gg=mb_done(&gable);add(root,gg,wall,true);t3_release(gg);
    prop(root,box,wall,x-2.5f,y+h+1.1f,z-1.0f,.7f,1.8f,.8f);
    for(int k=0;k<10;k++)prop(root,box,trim,x,y+.2f+k*(h-.2f)/10,z+2.915f,w,.025f,.025f);
    prop(root,box,dark,x+1.9f,y+1.18f,z+2.94f,2.8f,2.3f,.08f);
    for(int k=0;k<6;k++)prop(root,box,trim,x+1.9f,y+.25f+k*.37f,z+3,2.65f,.035f,.03f);
    prop(root,box,dark,x-1.0f,y+1,z+2.96f,.85f,2,.1f);
    for(int k=0;k<2;k++) {
      float wx=x-3.0f+k*2.7f,wy=y+h-.95f;
      prop(root,box,trim,wx,wy,z+2.97f,1.55f,1.35f,.13f);
      prop(root,box,(rnd()>.2f)?warm:dark,wx,wy,z+3.05f,1.35f,1.15f,.06f);
      prop(root,box,trim,wx,wy,z+3.10f,.055f,1.20f,.045f);prop(root,box,trim,wx,wy,z+3.10f,1.40f,.055f,.045f);
    }
    prop(root,box,concrete,x-1,y+.12f,z+3.7f,2,.24f,1.4f);
    prop(root,box,roof,x-1,y+2.5f,z+3.6f,2.3f,.15f,1.5f);
    for(int side=-1;side<=1;side+=2)prop(root,cyl,trim,x-1+side*.9f,y+1.3f,z+4.15f,.055f,2.6f,.055f);
    prop(root,ball,warm,x-.35f,y+2.05f,z+3.12f,.10f,.16f,.10f);
    if(!row){
      float my=bank_surface_y(x-5,-4.6f,4);prop(root,cyl,trim,x-5,my+.52f,-4.6f,.045f,1.04f,.045f);prop(root,box,metal,x-5,my+1.1f,-4.6f,.48f,.30f,.32f);
      prop(root,box,concrete,x+1.9f,y+.02f,-5.7f,3.1f,.10f,3.1f);
      for(int k=0;k<9;k++){float fx=x-4.8f+k*.42f,fy=bank_surface_y(fx,-4.1f,4);prop(root,box,trim,fx,fy+.45f,-4.1f,.075f,.9f,.075f);}
    }
  }
  /* Warm street lamps punctuate the cool moonlight; real light pools at the verge. */
  for(float x=10;x<t->finish_x;x+=32) {
    float y=track_height_at(x,NULL);prop(root,cyl,metal,x,y+2.5f,-3.7f,.065f,5,.065f);
    liberty_beam(root,cyl,metal,(t3_vec3){x,y+4.8f,-3.7f},(t3_vec3){x,y+5,-2.7f},.06f);
    prop(root,box,metal,x,y+4.97f,-2.65f,.65f,.15f,.43f);prop(root,box,warm,x,y+4.88f,-2.65f,.5f,.03f,.32f);
    if(x==10||x==170||x==234) {cw_node n=cw_node_new("OmniLight3D","PorchPool");cw_node_add_child(parent,n);cw_node3d_set_position(n,(t3_vec3){x,y+3.8f,-2.8f});cw_node_set(n,"color",cw_val_color(1,.64f,.27f,1));cw_node_set(n,"energy",cw_val_float(3));cw_node_set(n,"range",cw_val_float(8));}
  }
  for(int i=0;i<42;i++){float x=-15+i*10.4f,z=i%2?-17:-6.0f;if(x>56&&x<110)continue;burbs_tree(root,foliage,cyl,bark,leaves,x,z,i%3!=0);}
  /* Distant smaller neighborhoods and trees create layered silhouettes. */
  for(int i=0;i<32;i++){float x=-50+i*16,z=-48,y=bank_surface_y(x,z,4);prop(root,box,siding[i%3],x,y+2,z,9,4,6);for(int side=-1;side<=1;side+=2){t3_mesh *r=prop(root,box,roof,x,y+4.6f,z+side*1.6f,10,.2f,3.7f);t3_object_set_rotation(&r->base,side*.4f,0,0);}prop(root,foliage,leaves[0],x+6,y+5,z,3.5f,4,3);}
  /* Celestial distance removes street-scale parallax. Deliberately oversized
   * angular diameter gives the flight its cinematic silhouette. */
  t3_texture *tx=burbs_moon_texture();t3_material *moon=t3_mesh_basic_material_new(0xffffff);t3_material_set_map(moon,tx);moon->fog=false;moon->color=(t3_color){1.35f,1.40f,1.48f};t3_release(tx);
  t3_geometry *disk=t3_sphere_geometry_new(800,96,64);
  t3_attribute *normals=disk->attributes[T3_ATTR_NORMAL];float *nn=normals->array,*cc=malloc(normals->count*3*sizeof(float));
  for(int i=0;i<normals->count;i++){float k=.55f+.45f*sqrtf(fmaxf(0,nn[i*3]));cc[i*3]=cc[i*3+1]=cc[i*3+2]=k;}
  set_attr(disk,T3_ATTR_COLOR,t3_attribute_new(T3_FLOAT32,cc,normals->count,3));free(cc);moon->vertex_colors=true;
  t3_mesh *mm=add(root,disk,moon,false);mm->base.position=(t3_vec3){500,440,-5000};t3_object_set_rotation(&mm->base,0,-1.5707963f,0);t3_release(disk);t3_release(moon);
  t3_texture *ht=burbs_moon_halo();t3_material *halo=t3_mesh_basic_material_new(0xffffff);t3_material_set_map(halo,ht);halo->transparent=true;halo->depth_write=false;halo->blending=T3_ADDITIVE_BLENDING;halo->fog=false;t3_release(ht);
  t3_geometry *hg=t3_plane_geometry_new(3600,3600,1,1);t3_mesh *hm=add(root,hg,halo,false);hm->base.position=(t3_vec3){500,440,-5900};t3_release(hg);t3_release(halo);
  t3_material *star=t3_mesh_basic_material_new(0x8098c4);star->fog=false;
  for(int i=0;i<260;i++){float x=-9000+rnd()*18000,y=400+rnd()*7000,z=-9000;float r=1.3f+rnd()*2.5f;prop(root,ball,star,x,y,z,r,r,r);}
  t3_release(foliage);t3_release(star);t3_release(box);t3_release(ball);t3_release(cyl);t3_release(trim);t3_release(roof);t3_release(warm);t3_release(dark);t3_release(metal);t3_release(concrete);t3_release(bark);
  for(int i=0;i<3;i++){t3_release(siding[i]);t3_release(leaves[i]);}
}
