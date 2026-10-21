/* Street furniture and full-size sedans. Static clustered lights stay at
 * their authored locations for every multiplayer camera. */
static void car_quad(mb *b,t3_vec3 a,t3_vec3 c,t3_vec3 d,t3_vec3 e) {
  t3_vec3 u={c.x-a.x,c.y-a.y,c.z-a.z},v={d.x-a.x,d.y-a.y,d.z-a.z};
  t3_vec3 n={u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
  int k=mb_v(b,a,n,0,0,1);mb_v(b,c,n,1,0,1);mb_v(b,d,n,1,1,1);mb_v(b,e,n,0,1,1);
  mb_t(b,k,k+1,k+2);mb_t(b,k,k+2,k+3);
}
/* Chamfered cross-sections along the length, with a low bonnet and trunk. */
static t3_geometry *car_shell(bool cabin) {
  static const float body[][4]={{-2.3f,.45f,.75f,.75f},{-2.23f,.40f,.83f,.82f},{-2.05f,.35f,.91f,.88f},{-1.25f,.35f,1,.91f},{1.15f,.35f,.96f,.91f},{2.05f,.39f,.83f,.86f},{2.23f,.43f,.76f,.79f},{2.3f,.47f,.69f,.72f}};
  static const float roof[][4]={{-1.35f,.94f,.98f,.8f},{-.75f,.96f,1.48f,.74f},{.48f,.94f,1.48f,.74f},{1.18f,.92f,.96f,.8f}};
  /* Extra stations follow the circular wheel openings. These are cut into
   * the body silhouette rather than hidden behind a black disc. */
  static const float body_x[]={-2.3f,-2.23f,-2.05f,-1.85f,-1.80f,-1.70f,-1.55f,-1.43f,-1.30f,-1.15f,-1.06f,-1.01f,
                              -.6f,.6f,1.01f,1.06f,1.15f,1.30f,1.43f,1.55f,1.70f,1.80f,1.85f,2.05f,2.23f,2.3f};
  int count=cabin?4:(int)(sizeof body_x/sizeof body_x[0]);
  t3_vec3 rings[26][8];mb b={0};
  for(int i=0;i<count;i++) {
    float x,lo,hi,w,ch=cabin?.025f:.065f;
    if(cabin) { x=roof[i][0];lo=roof[i][1];hi=roof[i][2];w=roof[i][3]; }
    else {
      x=body_x[i];int section=0;
      while(section<6 && x>body[section+1][0])section++;
      float u=(x-body[section][0])/(body[section+1][0]-body[section][0]);
      lo=body[section][1]+u*(body[section+1][1]-body[section][1]);
      hi=body[section][2]+u*(body[section+1][2]-body[section][2]);
      w=body[section][3]+u*(body[section+1][3]-body[section][3]);
      float dx=fabsf(x)-1.43f;
      if(fabsf(dx)<.42f)lo=fmaxf(lo,.35f+sqrtf(.42f*.42f-dx*dx));
    }
    t3_vec3 r[]={{x,lo,-w+ch},{x,lo+ch,-w},{x,hi-ch,-w},{x,hi,-w+ch},{x,hi,w-ch},{x,hi-ch,w},{x,lo+ch,w},{x,lo,w-ch}};
    memcpy(rings[i],r,sizeof r);
  }
  for(int i=1;i<count;i++)for(int j=0;j<8;j++)car_quad(&b,rings[i-1][j],rings[i-1][(j+1)%8],rings[i][(j+1)%8],rings[i][j]);
  for(int j=1;j<7;j++) {
    car_quad(&b,rings[0][0],rings[0][j],rings[0][j+1],rings[0][j+1]);
    car_quad(&b,rings[count-1][0],rings[count-1][j+1],rings[count-1][j],rings[count-1][j]);
  }
  return mb_done(&b);
}
/* Shared roof crown and painted A/C pillars. The glass remains a single
 * opaque reflective shell, avoiding transparent sorting/overdraw per car. */
static t3_geometry *car_roof_frame(void) {
  mb b={0};
  const float xs[4]={-.82f,-.70f,.40f,.55f};
  const float ys[4]={1.46f,1.49f,1.49f,1.46f};
  for(int i=0;i<3;i++)for(int j=0;j<6;j++) {
    float za=-.76f+j*(1.52f/6),zb=za+1.52f/6;
    float ha=.055f*(1-za*za/(.76f*.76f));
    float hb=.055f*(1-zb*zb/(.76f*.76f));
    float slope=-(ys[i+1]-ys[i])/(xs[i+1]-xs[i]);
    t3_vec3 na={slope,1,.11f*za/(.76f*.76f)},nb={slope,1,.11f*zb/(.76f*.76f)};
    int k=mb_v(&b,(t3_vec3){xs[i],ys[i]+ha,za},na,0,0,1);
    mb_v(&b,(t3_vec3){xs[i],ys[i]+hb,zb},nb,0,1,1);
    mb_v(&b,(t3_vec3){xs[i+1],ys[i+1]+hb,zb},nb,1,1,1);
    mb_v(&b,(t3_vec3){xs[i+1],ys[i+1]+ha,za},na,1,0,1);
    mb_t(&b,k,k+1,k+2);mb_t(&b,k,k+2,k+3);
  }
  for(int side=-1;side<=1;side+=2) {
    for(int front=0;front<2;front++) {
      float top=front?.48f:-.75f,bottom=front?1.18f:-1.35f;
      float lo=front?.96f:.98f,half=front?.055f:.09f;
      t3_vec3 a={bottom-half,lo,side*.812f},c={bottom+half,lo,side*.812f};
      t3_vec3 d={top+half,1.49f,side*.755f},e={top-half,1.49f,side*.755f};
      if(side>0)car_quad(&b,a,c,d,e);else car_quad(&b,e,d,c,a);
    }
    /* Roof rail above the side glass. */
    t3_vec3 a={-.82f,1.435f,side*.76f},c={.55f,1.435f,side*.76f};
    t3_vec3 d={.55f,1.49f,side*.76f},e={-.82f,1.49f,side*.76f};
    if(side>0)car_quad(&b,a,c,d,e);else car_quad(&b,e,d,c,a);
  }
  return mb_done(&b);
}
/* Road tire: flat tread and shoulders instead of a circular torus section.
 * Same 20 x 8 segments (320 triangles), shared by all four wheels/all cars. */
static t3_geometry *car_tire(void) {
  static const float profile[8][2]={
    {.215f,-.09f},{.30f,-.11f},{.342f,-.075f},{.35f,-.055f},
    {.35f,.055f},{.342f,.075f},{.30f,.11f},{.215f,.09f}
  };
  mb b={0};
  for(int i=0;i<=20;i++) {
    float a=i*6.2831853f/20,co=cw_cos(a),si=cw_sin(a);
    for(int j=0;j<8;j++) {
      int prev=(j+7)%8,next=(j+1)%8;
      float dr=profile[next][0]-profile[prev][0];
      float dz=profile[next][1]-profile[prev][1];
      float inv=1/sqrtf(dr*dr+dz*dz);
      mb_v(&b,(t3_vec3){profile[j][0]*co,profile[j][0]*si,profile[j][1]},
           (t3_vec3){co*dz*inv,si*dz*inv,-dr*inv},(float)i/20,(float)j/8,1);
    }
  }
  for(int i=0;i<20;i++)for(int j=0;j<8;j++) {
    int a=i*8+j,c=(i+1)*8+j,d=(i+1)*8+(j+1)%8,e=i*8+(j+1)%8;
    mb_t(&b,a,c,d);mb_t(&b,a,d,e);
  }
  return mb_done(&b);
}

/* Extrude a counter-clockwise wheel-face polygon about z=0. */
static void car_rim_prism(mb *b,const t3_vec3 *v,int count,float depth) {
  for(int side=-1;side<=1;side+=2) {
    int first=-1;
    for(int j=0;j<count;j++) {
      int k=mb_v(b,(t3_vec3){v[j].x,v[j].y,side*depth},(t3_vec3){0,0,side},0,0,1);
      if(j==0)first=k;
    }
    for(int j=1;j<count-1;j++) {
      if(side>0)mb_t(b,first,first+j,first+j+1);
      else mb_t(b,first,first+j+1,first+j);
    }
  }
  for(int j=0;j<count;j++) {
    t3_vec3 a=v[j],c=v[(j+1)%count],d=c,e=a;
    a.z=c.z=-depth;d.z=e.z=depth;
    car_quad(b,a,c,d,e);
  }
}
static t3_geometry *car_rim(void) {
  mb b={0};
  const float section[4][2]={{.18f,-.025f},{.215f,-.025f},{.215f,.025f},{.18f,.025f}};
  for(int i=0;i<20;i++)for(int j=0;j<4;j++) {
    float a=i*6.2831853f/20,c=(i+1)*6.2831853f/20;
    int k=(j+1)%4;
    car_quad(&b,(t3_vec3){section[j][0]*cw_cos(a),section[j][0]*cw_sin(a),section[j][1]},
      (t3_vec3){section[j][0]*cw_cos(c),section[j][0]*cw_sin(c),section[j][1]},
      (t3_vec3){section[k][0]*cw_cos(c),section[k][0]*cw_sin(c),section[k][1]},
      (t3_vec3){section[k][0]*cw_cos(a),section[k][0]*cw_sin(a),section[k][1]});
  }
  for(int i=0;i<5;i++) {
    float a=i*6.2831853f/5,co=cw_cos(a),si=cw_sin(a);
    t3_vec3 v[4]={{-.032f,.035f,0},{.032f,.035f,0},{.022f,.187f,0},{-.022f,.187f,0}};
    for(int j=0;j<4;j++) {float x=v[j].x,y=v[j].y;v[j].x=x*co-y*si;v[j].y=x*si+y*co;}
    car_rim_prism(&b,v,4,.018f);
  }
  t3_vec3 hub[10];
  for(int i=0;i<10;i++){float a=i*6.2831853f/10;hub[i]=(t3_vec3){.058f*cw_cos(a),.058f*cw_sin(a),0};}
  car_rim_prism(&b,hub,10,.03f);
  return mb_done(&b);
}

static void city_cars(t3_object *root,const whimsy_parts *p) {
  t3_geometry *body=car_shell(false),*cabin=car_shell(true),*roof=car_roof_frame();
  t3_geometry *tire=car_tire(),*rim=car_rim();
  t3_geometry *disc=t3_cylinder_geometry_new(1,1,1,20,1,false);
  t3_material *paint[4]={toy_paint(.68f,.7f,.73f),toy_paint(.24f,.32f,.42f),toy_paint(.38f,.07f,.09f),toy_paint(.8f,.78f,.71f)};
  for(int i=0;i<4;i++){paint[i]->metalness=.48f;paint[i]->roughness=.27f;paint[i]->clearcoat=.85f;}
  t3_material *glass=toy_paint(.055f,.085f,.12f),*rubber=toy_paint(.025f,.028f,.032f),*alloy=toy_paint(.48f,.52f,.56f);
  glass->emissive=lin(.1f,.17f,.23f);glass->emissive_intensity=.4f;glass->metalness=.65f;glass->roughness=.13f;rubber->clearcoat=0;rubber->roughness=.94f;alloy->metalness=.8f;alloy->roughness=.24f;
  t3_material *head=glow(.8f,.9f,1,3),*tail=glow(1,.025f,.01f,2.2f);
  for(int i=0;i<17;i++) {
    float x=8+i*19,z=i%2?-7.4f:-4.8f,y=bank_y(x,z,2)+.12f;
    int first=root->child_count; t3_material *m=paint[i%4];
    prop(root,body,m,x,y,z,1,1,1);prop(root,cabin,glass,x,y,z,1,1,1);
    prop(root,roof,m,x,y,z,1,1,1);
    /* Lower windscreen seals: dark rubber breaks the glass/body boundary. */
    prop(root,p->box,rubber,x+1.16f,y+.975f,z,.055f,.035f,1.52f);
    prop(root,p->box,rubber,x-1.33f,y+.995f,z,.055f,.035f,1.52f);
    for(int side=-1;side<=1;side+=2) {
      float sz=z+side*.755f;
      prop(root,p->box,rubber,x-.15f,y+1.23f,sz,.095f,.49f,.04f); /* B pillar */
      prop(root,p->box,alloy,x-.12f,y+.96f,z+side*.88f,2.35f,.035f,.035f);
      for(int door=0;door<2;door++) {
        prop(root,p->box,alloy,x-.8f+door*1.05f,y+.83f,z+side*.92f,.2f,.035f,.04f);
        prop(root,p->box,rubber,x-.22f+door*1.2f,y+.66f,z+side*.918f,.012f,.45f,.012f);
      }
      prop(root,p->ball,m,x+.83f,y+1.02f,z+side*.99f,.15f,.09f,.13f);
      for(int axle=-1;axle<=1;axle+=2) {
        float wx=x+axle*1.43f,wz=z+side*.92f;
        t3_mesh *well=prop(root,disc,rubber,wx,y+.36f,wz,.405f,.055f,.405f);
        t3_object_set_rotation(&well->base,1.5707963f,0,0);
        prop(root,tire,rubber,wx,y+.35f,wz+side*.055f,1,1,1);
        prop(root,rim,alloy,wx,y+.35f,wz+side*.09f,1,1,1);
      }
      prop(root,p->box,head,x+2.23f,y+.69f,z+side*.56f,.1f,.12f,.33f);
      prop(root,p->box,tail,x-2.24f,y+.72f,z+side*.58f,.1f,.14f,.3f);
    }
    prop(root,p->box,rubber,x+2.3f,y+.53f,z,.035f,.13f,.82f);
    prop(root,p->box,alloy,x+2.32f,y+.63f,z,.025f,.027f,.82f);
    prop(root,p->box,p->cream,x-2.32f,y+.55f,z,.025f,.13f,.3f);
    /* Alternating directions; pitch the entire car to the street grade. */
    float slope=cw_atan2(bank_y(x+1,z,2)-bank_y(x-1,z,2),2);
    for(int k=first;k<root->child_count;k++) {
      t3_object *o=root->children[k];float dx=o->position.x-x,dy=o->position.y-y,dz=o->position.z-z;
      if(i%2){dx=-dx;dz=-dz;}
      o->position=(t3_vec3){x+dx*cw_cos(slope)-dy*cw_sin(slope),y+dx*cw_sin(slope)+dy*cw_cos(slope),z+dz};
      t3_quat turn=t3_quat_multiply(t3_quat_from_axis_angle((t3_vec3){0,0,1},slope),t3_quat_from_axis_angle((t3_vec3){0,1,0},i%2?3.14159265f:0));
      t3_object_set_quaternion(o,t3_quat_multiply(turn,o->quaternion));
    }
  }
  for(int i=0;i<4;i++)t3_release(paint[i]);
  t3_release(body);t3_release(cabin);t3_release(roof);t3_release(tire);t3_release(rim);t3_release(disc);
  t3_release(glass);t3_release(rubber);t3_release(alloy);t3_release(head);t3_release(tail);
}
