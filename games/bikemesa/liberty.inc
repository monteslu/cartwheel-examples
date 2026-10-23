/* Liberty Leap: an outdoor American stunt arena. Presentation only; the
 * course, collisions and jump trajectories remain owned by the track. */
static t3_mesh *stunt_part(t3_object *root,t3_geometry *g,t3_material *m,
                          float x,float y,float z,float sx,float sy,float sz) {
  t3_mesh *p=add(root,g,m,true);p->base.position=(t3_vec3){x,y,z};
  p->base.scale=(t3_vec3){sx,sy,sz};return p;
}
static t3_material *stunt_color(float r,float g,float b,float light) {
  t3_material *m=t3_mesh_standard_material_new(0xffffff);
  m->color=lin(r,g,b);m->roughness=.48f;
  if(light>0){m->emissive=lin(r,g,b);m->emissive_intensity=light;}
  return m;
}
/* The actual flag: 13 stripes, 50 five-point stars in nine alternating rows,
 * 1:1.9 proportions and a union seven stripes tall. One shared cloth texture. */
static t3_texture *liberty_flag_texture(void) {
  enum{W=760,H=400};uint8_t *px=malloc(W*H*4);
  for(int y=0;y<H;y++)for(int x=0;x<W;x++) {
    uint8_t *p=px+4*(y*W+x);bool red=(y*13/H)%2==0,union_=x<304&&y<H*7/13;
    p[0]=union_?22:red?179:245;p[1]=union_?40:red?25:242;p[2]=union_?78:red?45:229;p[3]=255;
  }
  for(int row=0;row<9;row++)for(int col=0;col<(row%2?5:6);col++) {
    float cx=(col*2+(row%2?2:1))*304.f/12,cy=(row+1)*(H*7.f/13)/10;
    float vx[10],vy[10];for(int i=0;i<10;i++) {
      float a=i*3.14159265f/5-1.5707963f,r=i%2?3.5f:8.3f;
      vx[i]=cx+cosf(a)*r;vy[i]=cy+sinf(a)*r;
    }
    for(int y=(int)cy-9;y<=(int)cy+9;y++)for(int x=(int)cx-9;x<=(int)cx+9;x++) {
      bool in=false;for(int i=0,j=9;i<10;j=i++)
        if((vy[i]>y+.5f)!=(vy[j]>y+.5f) && x+.5f<(vx[j]-vx[i])*(y+.5f-vy[i])/(vy[j]-vy[i])+vx[i])in=!in;
      if(in&&x>=0&&y>=0&&x<W&&y<H){uint8_t *p=px+4*(y*W+x);p[0]=p[1]=p[2]=250;}
    }
  }
  t3_texture *tx=t3_texture_new(W,H,px);free(px);tx->color_space=T3_SRGB_COLOR_SPACE;
  tx->generate_mipmaps=true;tx->min_filter=T3_LINEAR_MIPMAP_LINEAR;return tx;
}
static void liberty_flag(t3_object *root,t3_material *cloth,t3_material *metal,
                         float x,float y,float z,float height,float width) {
  t3_geometry *pole=t3_cylinder_geometry_new(.045f,.07f,height,12,1,false);
  stunt_part(root,pole,metal,x,y+height*.5f,z,1,1,1);t3_release(pole);
  t3_geometry *fin=t3_sphere_geometry_new(.105f,12,8);
  stunt_part(root,fin,metal,x,y+height+.08f,z,1,1,1);t3_release(fin);
  float h=width/1.9f;t3_geometry *g=t3_plane_geometry_new(width,h,32,12);
  t3_mesh *f=stunt_part(root,g,cloth,x+width*.5f+.07f,y+height-h*.5f-.12f,z,1,1,1);
  f->base.frustum_culled=false;
  if(n_flags<(int)CW_ARRAY_LEN(flags))flags[n_flags++]=(flag){g,width,h};
  t3_release(g);
}
static void liberty_beam(t3_object *root,t3_geometry *g,t3_material *m,
                         t3_vec3 a,t3_vec3 b,float width) {
  float dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z,l=sqrtf(dx*dx+dy*dy+dz*dz);
  t3_mesh *p=stunt_part(root,g,m,(a.x+b.x)*.5f,(a.y+b.y)*.5f,(a.z+b.z)*.5f,width,l,width);
  /* Supports lie in XY or YZ, enough for the arena's two truss axes. */
  if(fabsf(dz)>.001f)t3_object_set_rotation(&p->base,atan2f(dz,dy),0,0);
  else t3_object_set_rotation(&p->base,0,0,-atan2f(dx,dy));
}
static void liberty_fire(cw_node parent,float x,float y) {
  static const char *const flames[]={"amount","700","lifetime","0.65","lifetime_random","0.5","preprocess","1",
    "emission_shape","RING","emission_radius","2.56","emission_inner_radius","2.40",
    "direction","vec3(1,0,0)","spread","22","speed_min","1.0","speed_max","2.8",
    "gravity","vec3(0,2.2,0)","turbulence","1.6","turbulence_scale","0.45","turbulence_speed","2.5",
    "size","0.25","size_random","0.65","draw_mode","STRETCHED","stretch","0.22",
    "size_curve","\"0 0.4, 0.18 1, 0.55 0.65, 1 0\"","blend","ADD","energy","1.8",
    "color_ramp","\"0 1 0.7 0.23 0, 0.12 1 0.52 0.13 0.75, 0.4 1 0.20 0.012 0.60, 0.8 0.6 0.05 0.005 0.3, 1 0.2 0.01 0 0\"",NULL};
  static const char *const sparks[]={"amount","100","lifetime","1.6","preprocess","2",
    "emission_shape","RING","emission_radius","2.58","emission_inner_radius","2.42",
    "direction","vec3(1,0,0)","spread","32","speed_min","1.7","speed_max","4.0","gravity","vec3(0,-0.9,0)",
    "turbulence","1.1","size","0.035","size_random","0.6","draw_mode","STRETCHED","stretch","0.08",
    "blend","ADD","energy","4","color_ramp","\"0 1 0.65 0.15 0, 0.15 1 0.6 0.1 1, 1 1 0.08 0.01 0\"",NULL};
  static const char *const smoke[]={"amount","100","lifetime","2.5","preprocess","3",
    "emission_shape","RING","emission_radius","2.55","emission_inner_radius","2.4",
    "direction","vec3(1,0,0)","spread","20","speed_min",".5","speed_max","1.2","gravity","vec3(.15,.4,0)",
    "turbulence",".7","size",".32","size_random",".6","size_curve","\"0 .3, .3 1, 1 3\"",
    "blend","ALPHA","color_ramp","\"0 .12 .11 .10 0, .2 .16 .15 .14 .15, 1 .22 .22 .25 0\"",NULL};
  const char *const *settings[]={flames,sparks,smoke};const char *names[]={"Liberty ring fire","Liberty ring sparks","Liberty ring smoke"};
  for(int i=0;i<3;i++) {
    cw_node n=particles(parent,names[i],settings[i]);cw_node3d_set_position(n,(t3_vec3){x,y,0});
    /* Particle RING lies in XZ; rotate its normal into the riding axis X. */
    cw_node_set(n,"rotation",cw_val_vec3(0,0,1.5707963f));cw_node_set(n,"seed",cw_val_int(180+i));
  }
  cw_node light=cw_node_new("OmniLight3D","Ring firelight");cw_node_add_child(parent,light);
  cw_node_set(light,"position",cw_val_vec3(x,y,0));cw_node_set(light,"color",cw_val_color(1,.22f,.025f,1));
  cw_node_set(light,"energy",cw_val_float(5));cw_node_set(light,"range",cw_val_float(10));
}
/* Layered desert ridges give the arena a horizon and atmospheric depth. */
static void liberty_horizon(t3_object *root) {
  for(int layer=2;layer>=0;layer--) {
    mb b={0};enum{NX=160,NZ=12};
    for(int iz=0;iz<=NZ;iz++)for(int ix=0;ix<=NX;ix++) {
      float x=-250+ix*6.f,z=-65-layer*85-iz*8.f;
      float ridge=48+18*sinf(x*.019f+layer)+15*sinf(x*.047f+layer*2)*sinf(x*.047f+layer*2)+6*sinf(x*.11f);
      float u=(float)iz/NZ,profile=powf(sinf(u*3.14159265f),1.35f);
      mb_v(&b,(t3_vec3){x,-42+ridge*profile,z},(t3_vec3){0,1,0},ix*.1f,iz*.1f,1);
    }
    for(int iz=0;iz<NZ;iz++)for(int ix=0;ix<NX;ix++) {
      int a=iz*(NX+1)+ix,c=a+NX+1;mb_t(&b,a,a+1,c);mb_t(&b,a+1,c+1,c);
    }
    t3_geometry *g=mb_done(&b);t3_geometry_compute_vertex_normals(g);
    const float ridge_colors[3][3]={{.86f,.47f,.29f},{.74f,.53f,.44f},{.62f,.59f,.64f}};
    t3_material *m=stunt_color(ridge_colors[layer][0],ridge_colors[layer][1],ridge_colors[layer][2],0);
    tex_apply(m,TEX_ROCK,3,3,.35f,.3f);m->roughness=1;
    add(root,g,m,false);t3_release(g);t3_release(m);
  }
}
static void stuntshow_build(cw_node parent,t3_object *root,const track_desc *t) {
  if(strcmp(t->theme,"stuntshow"))return;
  liberty_horizon(root);
  t3_geometry *box=t3_box_geometry_new(1,1,1,1,1,1),*wheel=t3_cylinder_geometry_new(.49f,.49f,.24f,20,1,false);
  t3_material *navy=stunt_color(.035f,.07f,.14f,0),*red=stunt_color(.68f,.035f,.055f,0),*white=stunt_color(.9f,.9f,.83f,0);
  t3_material *steel=stunt_color(.42f,.47f,.52f,0),*concrete=stunt_color(.48f,.49f,.47f,0),*black=stunt_color(.018f,.024f,.03f,0);
  t3_material *yellow=stunt_color(.95f,.56f,.035f,0),*glass=stunt_color(.09f,.17f,.22f,0),*lamp=stunt_color(1,.83f,.55f,5);
  steel->metalness=.8f;steel->roughness=.28f;glass->metalness=.45f;glass->roughness=.15f;
  tex_apply(concrete,TEX_CONCRETE,2,2,.16f,.45f);
  t3_texture *tx=liberty_flag_texture();t3_material *cloth=stunt_color(1,1,1,0);
  tex_apply(cloth,TEX_COTTON,1,1,.10f,.2f);t3_material_set_map(cloth,tx);cloth->side=T3_DOUBLE_SIDE;t3_release(tx);
  t3_material *skin[3]={stunt_color(.63f,.36f,.20f,0),stunt_color(.9f,.64f,.43f,0),stunt_color(.32f,.18f,.12f,0)};
  t3_geometry *person=t3_sphere_geometry_new(1,10,8);
  /* A continuous arena backdrop: structural terraces, individual seats, aisles,
   * roof trusses, railings and illuminated fascia, close enough to read while riding. */
  for(int section=0;section<12;section++) {
    float x=-12+section*32,y=ground_at(x)-2.8f;
    stunt_part(root,box,navy,x,y+1.2f,-9.3f,30,2.4f,.4f);
    stunt_part(root,box,white,x,y+2.46f,-9.22f,30,.13f,.5f);
    for(int row=0;row<7;row++) {
      float yy=y+row*.8f,zz=-10.4f-row*1.25f;
      stunt_part(root,box,concrete,x,yy,zz,30,.28f,1.3f);
      for(int seat=0;seat<30;seat++)if(seat!=14&&seat!=15) {
        float xx=x-14.4f+seat*.98f;t3_material *c=row<2?red:row<4?white:navy;
        stunt_part(root,box,c,xx,yy+.32f,zz,.70f,.12f,.70f);
        stunt_part(root,box,c,xx,yy+.64f,zz-.32f,.70f,.62f,.10f);
        /* Restrained crowd silhouettes; enough empty seats to retain the stadium pattern. */
        if((seat*7+row*11+section)%7<2) {
          t3_material *shirt=(seat+row)%3?navy:red,*sk=skin[(seat+row+section)%3];
          stunt_part(root,person,shirt,xx,yy+.78f,zz+.03f,.23f,.34f,.15f);
          stunt_part(root,person,sk,xx,yy+1.25f,zz+.03f,.15f,.19f,.15f);
          for(int side=-1;side<=1;side+=2) {
            stunt_part(root,person,shirt,xx+side*.25f,yy+.80f,zz+.06f,.075f,.25f,.08f);
            stunt_part(root,person,sk,xx+side*.25f,yy+.56f,zz+.18f,.065f,.08f,.18f);
            stunt_part(root,person,navy,xx+side*.13f,yy+.32f,zz+.31f,.095f,.09f,.30f);
            stunt_part(root,person,navy,xx+side*.13f,yy+.08f,zz+.52f,.08f,.27f,.09f);
          }
        }
      }
    }
    for(int side=-1;side<=1;side+=2) {
      float xx=x+side*14.8f;
      stunt_part(root,box,steel,xx,y+4.6f,-17.8f,.18f,9.2f,.18f);
      liberty_beam(root,box,steel,(t3_vec3){xx,y+8.9f,-18},(t3_vec3){xx,y+8.1f,-8.6f},.16f);
      liberty_beam(root,box,steel,(t3_vec3){xx,y+5.6f,-17.8f},(t3_vec3){xx,y+8.1f,-9.5f},.10f);
    }
    if(section%3!=1) {
      t3_mesh *roof=stunt_part(root,box,navy,x,y+8.7f,-13.5f,31,.20f,10);
      t3_object_set_rotation(&roof->base,-.08f,0,0);
      stunt_part(root,box,red,x,y+8.1f,-8.5f,31,.55f,.16f);
      stunt_part(root,box,lamp,x,y+7.78f,-8.42f,29,.055f,.055f);
    }
    mesa_name(root,section%2?"LIBERTY LEAP":"BIKEMESA STUNT SHOW",x,y+1.25f,-9.05f,18,1.0f);
    liberty_flag(root,cloth,steel,x-13,y,-8.1f,6.5f,3.4f);
  }
  /* Stadium floodlights with actual lamp housings and cross-braced masts. */
  for(int i=0;i<7;i++) {
    float x=15+i*52,y=ground_at(x)-3.5f;
    for(int side=-1;side<=1;side+=2)stunt_part(root,box,steel,x+side*.4f,y+9,-22,.13f,18,.13f);
    for(int k=0;k<8;k++)liberty_beam(root,box,steel,(t3_vec3){x-.4f,y+k*2.1f,-22},(t3_vec3){x+.4f,y+(k+1)*2.1f,-22},.07f);
    stunt_part(root,box,black,x,y+18,-22,4.2f,1.9f,.5f);
    for(int a=0;a<5;a++)for(int b=0;b<2;b++)stunt_part(root,box,lamp,x-1.6f+a*.8f,y+17.55f+b*.9f,-21.72f,.6f,.65f,.06f);
  }
  /* Low red/white safety blocks and metal railings frame the course without
   * obscuring its riding surface. Skip gaps instead of bridging them visually. */
  for(float x=0;x<t->finish_x+18;x+=3) {
    bool found;float y=track_height_at(x,&found);if(!found)continue;
    stunt_part(root,box,((int)(x/3))%2?white:red,x,y+.36f,-3.35f,2.9f,.72f,.5f);
    stunt_part(root,box,steel,x,y+.94f,-3.4f,.065f,.85f,.065f);
    stunt_part(root,box,steel,x,y+1.34f,-3.4f,3.0f,.055f,.055f);
  }
  int gaps=0;
  for(int p=0;p+1<t->piece_count&&gaps<2;p++) {
    const track_piece *a=&t->pieces[p],*b=&t->pieces[p+1];
    float x0=a->pts[(a->count-1)*2],x1=b->pts[0];if(x1-x0<12)continue;
    float by=b->pts[1]-.8f;
    /* Recognizable school buses with bonnets, roof caps, bumpers and wheel hubs. */
    for(int j=0;j<2;j++) {
      float x=x0+(j+.5f)*(x1-x0)*.5f;
      stunt_part(root,box,black,x,by-1.1f,0,8.5f,.25f,2.25f);
      stunt_part(root,box,yellow,x-.3f,by,0,7.5f,2.15f,2.3f);
      stunt_part(root,box,yellow,x+3.9f,by-.5f,0,1.2f,1.15f,2.1f);
      stunt_part(root,box,yellow,x-.3f,by+1.13f,0,7.3f,.22f,2.18f);
      for(int side=-1;side<=1;side+=2) {
        float z=side*1.17f;
        for(int stripe=0;stripe<3;stripe++)stunt_part(root,box,black,x-.3f,by-.26f-stripe*.2f,z,7.4f,.065f,.025f);
        for(int w=0;w<7;w++) {
          stunt_part(root,box,steel,x-3.35f+w*.94f,by+.49f,z,.83f,.83f,.035f);
          stunt_part(root,box,glass,x-3.35f+w*.94f,by+.49f,z+side*.025f,.71f,.70f,.035f);
        }
        for(int w=0;w<2;w++) {
          t3_mesh *wh=stunt_part(root,wheel,black,x+(w?2.8f:-2.8f),by-1.05f,side*1.2f,1,1,1);
          t3_object_set_rotation(&wh->base,1.5707963f,0,0);
          t3_mesh *hub=stunt_part(root,wheel,steel,x+(w?2.8f:-2.8f),by-1.05f,side*1.34f,.45f,.15f,.45f);
          t3_object_set_rotation(&hub->base,1.5707963f,0,0);
        }
      }
      stunt_part(root,box,steel,x+4.55f,by-.9f,0,.16f,.20f,2.36f);
      stunt_part(root,box,black,x+4.52f,by-.4f,0,.035f,.55f,.85f);
      for(int side=-1;side<=1;side+=2)stunt_part(root,box,lamp,x+4.53f,by-.35f,side*.78f,.06f,.25f,.35f);
      mesa_name(root,"SCHOOL BUS",x-.4f,by-.15f,1.205f,3.7f,.28f);
    }
    /* Stage foundations anchor the vehicles and jump, rather than floating boxes. */
    stunt_part(root,box,concrete,(x0+x1)*.5f,by-1.75f,0,x1-x0+1,.35f,6.2f);
    /* Box-truss undercarriage makes the bus deck a built stunt stage. */
    stunt_part(root,box,navy,(x0+x1)*.5f,by-2.6f,0,x1-x0+1,1.45f,5.9f);
    for(float bx=x0;bx<x1;bx+=2.4f) {
      liberty_beam(root,box,steel,(t3_vec3){bx,by-3.3f,3.02f},(t3_vec3){fminf(bx+2.4f,x1),by-1.95f,3.02f},.10f);
      stunt_part(root,box,steel,bx,by-2.6f,3.02f,.10f,1.45f,.10f);
    }
    for(int side=-1;side<=1;side+=2)stunt_part(root,box,concrete,(x0+x1)*.5f+side*(x1-x0)*.32f,by-10,0,1.3f,16,5.5f);
    if(gaps==1) {
      float hx=(x0+x1)*.5f,hy=a->pts[(a->count-1)*2+1]+8.6f;
      t3_geometry *ring=t3_torus_geometry_new(2.45f,.10f,12,96,6.2831853f);
      t3_mesh *r=stunt_part(root,ring,steel,hx,hy,0,1,1,1);
      /* A bike travels along X: the ring's opening must lie in YZ. */
      t3_object_set_rotation(&r->base,0,1.5707963f,0);t3_release(ring);
      float bottom=by-1.55f;
      for(int side=-1;side<=1;side+=2) {
        float z=side*3.2f;
        stunt_part(root,box,concrete,hx,bottom,z,2,.5f,1.4f);
        stunt_part(root,box,steel,hx,(bottom+hy)*.5f,z,.16f,hy-bottom,.16f);
        liberty_beam(root,box,steel,(t3_vec3){hx,bottom,side*4.6f},(t3_vec3){hx,hy-.5f,z},.10f);
        liberty_beam(root,box,steel,(t3_vec3){hx,hy,z},(t3_vec3){hx,hy,side*2.45f},.12f);
      }
      liberty_fire(parent,hx,hy);
    }
    gaps++;
  }
  /* Finish gantry: an identifiable destination, with a scoreboard behind it. */
  float x=t->finish_x,y=t->finish_y;
  for(int side=-1;side<=1;side+=2) {
    stunt_part(root,box,navy,x,y+3.6f,side*3.0f,.50f,7.2f,.50f);
    stunt_part(root,box,white,x+.27f,y+3.6f,side*3.0f,.04f,6.8f,.13f);
    liberty_flag(root,cloth,steel,x+3,y,side*4.7f,8.5f,4.0f);
  }
  stunt_part(root,box,red,x,y+7.3f,0,.7f,.8f,6.5f);
  stunt_part(root,box,navy,x-5,y+6.2f,-8,14,4,.55f);
  stunt_part(root,box,white,x-5,y+8.25f,-7.65f,14.3f,.14f,.12f);
  mesa_name(root,"LIBERTY LEAP",x-5,y+6.8f,-7.7f,12,1.2f);
  mesa_name(root,"HOME OF THE BRAVE",x-5,y+5.3f,-7.7f,11,.65f);
  for(int i=0;i<3;i++)t3_release(skin[i]);t3_release(person);
  t3_geometry *allg[]={box,wheel};for(int i=0;i<2;i++)t3_release(allg[i]);
  t3_material *allm[]={navy,red,white,steel,concrete,black,yellow,glass,lamp,cloth};for(int i=0;i<10;i++)t3_release(allm[i]);
}
