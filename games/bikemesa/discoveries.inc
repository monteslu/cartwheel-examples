/* Discoveries are deliberately placed beyond the opening third of a course.
 * Keep scenery out of collision and batch each rigid animated body part. */
static void merge_still(t3_object *root,int a,int b,bool city);
static cw_node discovery_parent;
static t3_object *snow_limb(t3_object *root,const whimsy_parts *p,t3_material *fur,
                           float x,float y,float z,float length,float width,float phase) {
  t3_object *joint=t3_group_new();joint->position=(t3_vec3){x,y,z};t3_object_add(root,joint);
  prop(joint,p->ball,fur,0,-length*.45f,0,width,length*.6f,width);
  for(int j=0;j<16;j++) {
    float a=j*2.3999632f,h=(j%4)*length*.2f;
    t3_mesh *tuft=prop(joint,p->ball,fur,cw_cos(a)*width*.8f,-h,cw_sin(a)*width*.8f,.095f,.26f,.085f);
    t3_object_set_rotation(&tuft->base,0,0,3.1415927f);
  }
  merge_still(joint,0,joint->child_count,false);
  if(whimsy_motion_count<96)whimsy_motion_list[whimsy_motion_count++]=(whimsy_motion){.object=joint,.phase=phase,.kind=3};
  t3_release(joint);return joint;
}
static void snow_walker(t3_object *root,const whimsy_parts *p,float x,float z,bool white) {
  t3_material *fur=t3_mesh_standard_material_new(white?0xd7d9cf:0x645a51);fur->roughness=1;
  t3_object *body=t3_group_new();body->position=(t3_vec3){x,bank_y(x,z,1),z};
  body->scale=(t3_vec3){.8f,.8f,.8f};
  t3_object_set_rotation(body,0,2.4980915f,0);t3_object_add(root,body);
  prop(body,p->ball,fur,0,2.2f,0,.85f,1.15f,.5f);
  prop(body,p->ball,fur,0,3.03f,-.1f,.73f,.65f,.45f);
  /* Face turned back over a shoulder, heavy brow and shaggy crown. */
  prop(body,p->ball,fur,-.15f,3.65f,0,.43f,.52f,.4f);
  prop(body,p->ball,p->brown,-.43f,3.6f,.22f,.27f,.32f,.2f);
  prop(body,p->ball,p->black,-.53f,3.72f,.35f,.06f,.04f,.055f);
  prop(body,p->ball,fur,-.44f,3.84f,.3f,.27f,.11f,.19f);
  for(int i=0;i<70;i++) {
    float a=i*2.3999632f,h=1.45f+(i%10)*.22f+.08f*cw_sin(i*1.7f);
    t3_mesh *tuft=prop(body,p->ball,fur,cw_cos(a)*.72f,h,cw_sin(a)*.44f,.11f,.32f,.095f);
    t3_object_set_rotation(&tuft->base,0,0,3.1415927f);
  }
  merge_still(body,0,body->child_count,false);
  for(int side=-1;side<=1;side+=2) {
    t3_object *leg=snow_limb(body,p,fur,side*.4f,1.5f,0,1.3f,.28f,side<0?0:3.1415927f);
    t3_mesh *foot=prop(body,p->ball,fur,side*.4f,.2f,0,.32f,.2f,.52f);
    /* Replace the generic pendulum with terrain-aware ankle placement. */
    if(whimsy_motion_count && whimsy_motion_list[whimsy_motion_count-1].object==leg) {
      whimsy_motion *gait=&whimsy_motion_list[whimsy_motion_count-1];
      gait->kind=14;gait->phase=side<0?0:.5f;gait->foot=&foot->base;
    }
    t3_object *arm=snow_limb(body,p,fur,side*.92f,3,0,1.75f,.26f,side<0?3.1415927f:0);
    prop(arm,p->ball,fur,0,-1.65f,.06f,.22f,.28f,.2f);
  }
  if(whimsy_motion_count<96)whimsy_motion_list[whimsy_motion_count++]=(whimsy_motion){.object=body,.x=x,.y=z,.phase=white?0:3,.kind=4};
  t3_release(body);t3_release(fur);
  /* Project onto rendered depth: prints cross bank triangle seams without
   * floating geometry or depth fighting. Two lobes give a heel and broad toes. */
  for(int j=0;j<12;j++) {
    float px=x-3+j*.25f+(j%2?.25f:-.25f),pz=z+4-j*.34f;
    for(int lobe=0;lobe<2;lobe++) {
      float zz=pz+(lobe?-.13f:.16f);
      cw_node d=cw_node_new("Decal3D","Sasquatch snow impression");
      cw_node_add_child(discovery_parent,d);
      cw_node_set(d,"blend",cw_val_enum("MULTIPLY"));
      cw_node_set(d,"color",cw_val_color(.62f,.64f,.66f,.55f));
      cw_node_set(d,"size",cw_val_vec3(lobe?.36f:.25f,lobe?.42f:.32f,1.2f));
      cw_node_set(d,"normal_fade",cw_val_float(0));
      cw_node3d_set_position(d,(t3_vec3){px,bank_surface_y(px,zz,1),zz});
      cw_node3d_set_rotation(d,(t3_vec3){-1.5707963f,0,-.35f});
    }
  }
}
/* Beveled, fractured stone blocks: shared low-poly variants with flat faces. */
static t3_geometry *weathered_stone(int variant) {
  const float rim[8][2]={{-.32f,-.5f},{.32f,-.5f},{.5f,-.32f},{.5f,.32f},{.32f,.5f},{-.32f,.5f},{-.5f,.32f},{-.5f,-.32f}};
  t3_vec3 points[4][8];
  for(int row=0;row<4;row++)for(int j=0;j<8;j++) {
    float phase=variant*1.71f+j*2.31f;
    float inset=row==0?.91f:row==3?.78f:1;
    float rough=.035f*cw_sin(phase+row*1.5f);
    points[row][j]=(t3_vec3){rim[j][0]*inset+rough,-.5f+row/3.f+(row==3?.045f*cw_sin(phase):0),rim[j][1]*inset+rough*.7f};
  }
  mb out={0};
  for(int row=0;row<3;row++)for(int j=0;j<8;j++) {
    t3_vec3 a=points[row][j],b=points[row+1][j],c=points[row+1][(j+1)%8],d=points[row][(j+1)%8];
    t3_vec3 n=t3_vec3_normalize(t3_vec3_cross(t3_vec3_sub(b,a),t3_vec3_sub(c,a)));
    int v=mb_v(&out,a,n,j*.25f,row*.5f,1);
    mb_v(&out,b,n,j*.25f,(row+1)*.5f,1);mb_v(&out,c,n,(j+1)*.25f,(row+1)*.5f,1);mb_v(&out,d,n,(j+1)*.25f,row*.5f,1);
    mb_t(&out,v,v+1,v+2);mb_t(&out,v,v+2,v+3);
  }
  for(int top=0;top<2;top++)for(int j=1;j<7;j++) {
    t3_vec3 a=points[top?3:0][0],b=points[top?3:0][top?j+1:j],c=points[top?3:0][top?j:j+1];
    t3_vec3 n=t3_vec3_normalize(t3_vec3_cross(t3_vec3_sub(b,a),t3_vec3_sub(c,a)));
    int v=mb_v(&out,a,n,a.x,a.z,1);mb_v(&out,b,n,b.x,b.z,1);mb_v(&out,c,n,c.x,c.z,1);mb_t(&out,v,v+1,v+2);
  }
  return mb_done(&out);
}
static void stone_circle(t3_object *root,const whimsy_parts *p,float x,float z) {
  t3_material *stone=mat(TEX_CONCRETE,(float[3]){.65f,.64f,.53f},1,1,.45f,.98f);
  t3_material *moss=t3_mesh_standard_material_new(0x405530);moss->roughness=1;
  t3_geometry *shapes[6];for(int i=0;i<6;i++)shapes[i]=weathered_stone(i);
  for(int i=0;i<9;i++) {
    float a=i*6.2831853f/9,px=x+cw_cos(a)*6,pz=z+cw_sin(a)*2.6f;
    float y=bank_surface_y(px,pz,0),h=3.2f+(i%3)*.28f;
    for(int side=-1;side<=1;side+=2) {
      float dx=cw_sin(a)*side*1.05f,dz=-cw_cos(a)*side*1.05f;
      float base=bank_surface_y(px+dx,pz+dz,0)-.22f;
      float height=fmaxf(2,y+h-base);
      t3_mesh *s=prop(root,shapes[(i+(side>0))%6],stone,px+dx,base+height*.5f,pz+dz,1.5f,height,1.45f);
      t3_object_set_rotation(&s->base,.025f*side,-a,.035f*side);
      prop(root,p->ball,moss,px+dx,base+.24f,pz+dz,.85f,.2f,.85f);
      for(int k=0;k<5;k++) {
        float vy=base+.5f+k*.65f;
        prop(root,p->ball,moss,px+dx+.18f*cw_sin(k),vy,pz+dz+.67f,.14f,.4f,.07f);
      }
    }
    t3_mesh *lintel=prop(root,shapes[(i+3)%6],stone,px,(i==2?bank_surface_y(px,pz+1.5f,0)+.35f:y+h+.28f),pz+(i==2?1.5f:0),3.6f,.75f,1.4f);
    t3_object_set_rotation(&lintel->base,0,1.5707963f-a,i==2?.2f:.025f);
  }
  for(int i=0;i<3;i++) {
    float px=x-23+i*6,pz=z+2+(i%2)*3,y=bank_surface_y(px,pz,0);
    t3_mesh *s=prop(root,shapes[(i+2)%6],stone,px,y+1.05f,pz,1.4f,2.5f,1.4f);
    t3_object_set_rotation(&s->base,0,.3f*i,.12f*(i-1));
  }
  for(int i=0;i<6;i++)t3_release(shapes[i]);
  t3_release(stone);t3_release(moss);
}
/* Bake each block's dimensions before UV projection. A strip mapped around
 * a unit block stretches badly on the guardian's wide brow and chest. */
static t3_mesh *carved_stone(t3_object *root,t3_material *material,
                             float x,float y,float z,float sx,float sy,float sz) {
  t3_geometry *g=weathered_stone(4);
  t3_attribute *pos=g->attributes[T3_ATTR_POSITION],*normal=g->attributes[T3_ATTR_NORMAL],*uv=g->attributes[T3_ATTR_UV];
  float *v=pos->array,*n=normal->array,*t=uv->array;
  for(int i=0;i<pos->count;i++) {
    v[i*3]*=sx;v[i*3+1]*=sy;v[i*3+2]*=sz;
    t3_vec3 nn=t3_vec3_normalize((t3_vec3){n[i*3]/sx,n[i*3+1]/sy,n[i*3+2]/sz});
    n[i*3]=nn.x;n[i*3+1]=nn.y;n[i*3+2]=nn.z;
    /* Face-local planar projection, one repeat per 1.5 metres. */
    bool top=fabsf(nn.y)>fmaxf(fabsf(nn.x),fabsf(nn.z));
    bool side=fabsf(nn.x)>fabsf(nn.z);
    t[i*2]=(top||!side?v[i*3]:v[i*3+2])/1.5f;
    t[i*2+1]=(top?v[i*3+2]:v[i*3+1])/1.5f;
  }
  pos->version++;normal->version++;uv->version++;
  t3_mesh *mesh=prop(root,g,material,x,y,z,1,1,1);t3_release(g);return mesh;
}
/* An ancient guardian wakes as riders approach, then watches the trail.
 * Only its merged head moves; feet and ruin stones follow the actual bank. */
static void temple_guardian(t3_object *root,const whimsy_parts *p,float x,float z) {
  t3_material *rock=mat(TEX_MOSSROCK,(float[3]){.82f,.84f,.72f},1,1,.65f,.95f);
  t3_material *moss=t3_mesh_standard_material_new(0x354b26);moss->roughness=1;
  float y=bank_surface_y(x,z,0);
  /* Squat seated silhouette, with broad arms and individually grounded feet. */
  carved_stone(root,rock,x,y+1.65f,z,3.5f,2.9f,2.4f);
  for(int side=-1;side<=1;side+=2) {
    float fx=x+side*1.65f,fz=z+1.25f,fy=bank_surface_y(fx,fz,0);
    carved_stone(root,rock,fx,fy+.5f,fz,1.6f,1.1f,2.1f);
    carved_stone(root,rock,x+side*2.05f,y+1.8f,z+.4f,1.1f,2.2f,1.35f);
    for(int k=0;k<3;k++)prop(root,p->box,p->black,fx-.45f+k*.45f,fy+.55f,fz+1.06f,.055f,.45f,.025f);
    prop(root,p->ball,moss,x+side*1.9f,y+2.8f,z+.4f,.7f,.2f,.6f);
  }
  t3_object *head=t3_group_new();head->position=(t3_vec3){x,y+3.4f,z};head->scale=(t3_vec3){.85f,.8f,.85f};t3_object_add(root,head);
  carved_stone(head,rock,0,.5f,0,3.8f,2.6f,2.4f);
  carved_stone(head,rock,0,-.8f,.55f,2.7f,.65f,1.8f);
  prop(head,p->box,p->black,0,-.4f,1.2f,2.2f,.23f,.06f);
  carved_stone(head,rock,0,.1f,1.3f,.7f,1.1f,.8f);
  for(int side=-1;side<=1;side+=2) {
    prop(head,p->box,p->black,side*.95f,.65f,1.2f,1.05f,.6f,.12f);
    prop(head,p->box,p->lamp,side*.95f,.65f,1.28f,.64f,.17f,.06f);
    t3_mesh *brow=carved_stone(head,rock,side*.95f,1.06f,1.25f,1.4f,.43f,.45f);
    t3_object_set_rotation(&brow->base,0,0,side*.12f);
    carved_stone(head,rock,side*2,.55f,0,.55f,1.5f,1.1f);
  }
  for(int k=0;k<5;k++)carved_stone(head,rock,-1.5f+k*.75f,1.95f,0,.5f,.75f+(k%2)*.3f,1.3f);
  for(int k=0;k<8;k++)prop(head,p->ball,moss,-1.6f+k*.16f,1.6f-k*.19f,1.24f,.24f,.27f,.08f);
  merge_still(head,0,head->child_count,false);
  if(whimsy_motion_count<96)whimsy_motion_list[whimsy_motion_count++]=(whimsy_motion){.object=head,.x=x,.kind=10};
  t3_release(head);
  /* A broken procession of smaller stones leads into the forest behind it. */
  for(int i=0;i<7;i++) {
    float px=x-9+i*3.4f,pz=z-5-(i%3)*2,py=bank_surface_y(px,pz,0),h=1.2f+(i%3)*.8f;
    t3_mesh *m=carved_stone(root,rock,px,py+h*.4f,pz,1.1f,h,1.2f);
    t3_object_set_rotation(&m->base,0,i*.7f,.2f*(i%3-1));
  }
  t3_release(rock);t3_release(moss);
}
/* Small faceted thermal-blanket panels: creases live in the static mesh,
 * so they catch the existing sun without a normal-map texture or animation. */
static void lunar_foil(t3_object *root,t3_material *foil) {
  mb mesh={0};
  for(int face=0;face<8;face++) {
    float a=face*6.2831853f/8,b=(face+1)*6.2831853f/8;
    t3_vec3 pts[5][7];
    for(int j=0;j<=4;j++)for(int i=0;i<=6;i++) {
      float u=i/6.f,v=j/4.f;
      if(i>0&&i<6&&j>0&&j<4) {
        u+=.055f*cw_sin(i*17.31f+j*7.79f+face*4.1f);
        v+=.075f*cw_sin(i*3.19f+j*13.57f+face*9.3f);
      }
      float crease=(i==0||i==6||j==0||j==4)?0:
        .018f*(1+cw_sin(i*19.71f+j*43.13f+face*11.7f));
      float nx=cw_cos((a+b)*.5f),nz=cw_sin((a+b)*.5f);
      pts[j][i]=(t3_vec3){2.25f*((1-u)*cw_cos(a)+u*cw_cos(b))+nx*(.012f+crease),
        -.75f+v*1.5f,2.25f*((1-u)*cw_sin(a)+u*cw_sin(b))+nz*(.012f+crease)};
    }
    for(int j=0;j<4;j++)for(int i=0;i<6;i++)for(int half=0;half<2;half++) {
      t3_vec3 q[3]={pts[j][i],pts[j+1][i],pts[j+1][i+1]};
      if(half){q[1]=q[2];q[2]=pts[j][i+1];}
      t3_vec3 d={q[1].x-q[0].x,q[1].y-q[0].y,q[1].z-q[0].z};
      t3_vec3 e={q[2].x-q[0].x,q[2].y-q[0].y,q[2].z-q[0].z};
      t3_vec3 n={d.y*e.z-d.z*e.y,d.z*e.x-d.x*e.z,d.x*e.y-d.y*e.x};
      float length=sqrtf(n.x*n.x+n.y*n.y+n.z*n.z);n.x/=length;n.y/=length;n.z/=length;
      int k=mb_v(&mesh,q[0],n,0,0,1);mb_v(&mesh,q[1],n,0,1,1);mb_v(&mesh,q[2],n,1,1,1);
      mb_t(&mesh,k,k+1,k+2);
    }
  }
  t3_geometry *g=mb_done(&mesh);prop(root,g,foil,0,0,0,1,1,1);t3_release(g);
}
static void lunar_wreck(t3_object *root,const whimsy_parts *p,float x,float z) {
  float y=bank_surface_y(x,z,0);
  t3_material *foil=t3_mesh_standard_material_new(0xc99736);foil->metalness=.7f;foil->roughness=.48f;
  t3_material *silver=t3_mesh_standard_material_new(0xa9adb0);silver->metalness=.65f;silver->roughness=.5f;
  t3_material *dust=t3_mesh_standard_material_new(0x55534f);dust->roughness=1;
  t3_geometry *oct=t3_cylinder_geometry_new(1,1,1,8,1,false);
  t3_object *lander=t3_group_new();lander->position=(t3_vec3){x,y+1.3f,z};
  t3_object_set_rotation(lander,.12f,.25f,-.27f);t3_object_add(root,lander);
  prop(lander,oct,foil,0,0,0,2.25f,1.5f,2.25f);
  lunar_foil(lander,foil);
  prop(lander,oct,p->black,0,.8f,0,2.1f,.15f,2.1f);
  prop(lander,p->box,silver,0,1.8f,0,2.7f,1.8f,2);
  t3_mesh *roof=prop(lander,oct,silver,0,2.8f,0,1.5f,.85f,1.2f);
  t3_object_set_rotation(&roof->base,0,.392699f,0);
  prop(lander,p->box,p->black,-1.2f,1.5f,-.8f,1.1f,1.7f,1.2f);
  prop(lander,p->ball,silver,1.45f,1.6f,-.3f,.65f,.8f,.7f);
  /* Apollo's characteristic small triangular cockpit windows. */
  mb win={0};
  for(int side=-1;side<=1;side+=2) {
    float cx=side*.72f;
    int v0=mb_v(&win,(t3_vec3){cx-.43f,2.15f,1.025f},(t3_vec3){0,0,1},0,0,1);
    int v1=mb_v(&win,(t3_vec3){cx+.43f,2.15f,1.025f},(t3_vec3){0,0,1},1,0,1);
    int v2=mb_v(&win,(t3_vec3){cx+side*.27f,2.75f,1.025f},(t3_vec3){0,0,1},.5f,1,1);
    mb_t(&win,v0,v1,v2);
    for(int j=0;j<4;j++) {
      prop(lander,p->box,p->black,side*2.08f,-.3f+j*.23f,0,.035f,.04f,1.5f);
    }
    prop(lander,p->box,silver,side*1.45f,2.2f,-.7f,.3f,.15f,.3f);
  }
  t3_geometry *wg=mb_done(&win);prop(lander,wg,p->black,0,0,0,1,1,1);t3_release(wg);
  prop(lander,p->box,p->black,0,1.3f,1.05f,.7f,.75f,.07f);
  prop(lander,p->box,silver,0,.88f,1.45f,1.1f,.08f,.9f);
  for(int k=0;k<7;k++)prop(lander,p->box,silver,0,.7f-k*.35f,2.1f+k*.13f,.8f,.06f,.12f);
  for(int side=-1;side<=1;side+=2)circus_stay(lander,p,silver,(t3_vec3){side*.45f,.8f,2.1f},(t3_vec3){side*.45f,-1.5f,2.95f},.035f);
  prop(lander,p->cyl,silver,.7f,3.6f,0,.035f,1.2f,.035f);
  prop(lander,p->ball,silver,.7f,4.15f,0,.6f,.1f,.6f);
  t3_object_update_world_matrix(lander,true,false);
  /* Feet seat into the actual triangulated slope. Struts connect to the tilted
   * body in world space, allowing the wreck's gear to look buckled. */
  for(int side=-1;side<=1;side+=2)for(int back=-1;back<=1;back+=2) {
    t3_vec3 foot=t3_vec3_apply_mat4((t3_vec3){side*3.2f,-1.6f,back*3.2f},&lander->matrix_world);
    foot.y=bank_surface_y(foot.x,foot.z,0)+.08f;
    t3_vec3 socket=t3_vec3_apply_mat4((t3_vec3){side*1.3f,.4f,back*1.3f},&lander->matrix_world);
    circus_stay(root,p,silver,socket,foot,.085f);
    socket=t3_vec3_apply_mat4((t3_vec3){side*1.8f,-.6f,back*.8f},&lander->matrix_world);
    circus_stay(root,p,foil,socket,foot,.055f);
    prop(root,p->cyl,foil,foot.x,foot.y,foot.z,.6f,.15f,.6f);
  }
  merge_still(lander,0,lander->child_count,false);t3_release(lander);
  float ax=x+5.6f,az=z+2,ay=bank_surface_y(ax,az,0);
  prop(root,p->box,p->cream,ax,ay+1.5f,az-.3f,.8f,1.1f,.65f);
  prop(root,p->box,silver,ax,ay+1.5f,az-.65f,.65f,.8f,.09f);
  prop(root,p->ball,p->cream,ax,ay+1.3f,az,.65f,.8f,.4f);
  prop(root,p->ball,p->cream,ax,ay+2.35f,az,.55f,.55f,.5f);
  prop(root,p->ball,p->black,ax,ay+2.36f,az+.31f,.46f,.43f,.24f);
  prop(root,p->ball,foil,ax,ay+2.36f,az+.39f,.4f,.36f,.19f);
  prop(root,p->box,silver,ax,ay+1.7f,az+.4f,.55f,.3f,.08f);
  for(int j=0;j<3;j++)prop(root,p->ball,j==0?p->red:p->blue,ax-.16f+j*.16f,ay+1.7f,az+.46f,.035f,.035f,.02f);
  for(int side=-1;side<=1;side+=2) {
    prop(root,p->cyl,p->cream,ax+side*.3f,ay+.6f,az,.22f,1,.22f);
    prop(root,p->cyl,p->red,ax+side*.3f,ay+.8f,az,.225f,.09f,.225f);
    prop(root,p->box,p->cream,ax+side*.3f,ay+.15f,az+.2f,.5f,.3f,.7f);
    prop(root,p->box,p->black,ax+side*.3f,ay+.03f,az+.2f,.5f,.06f,.7f);
    circus_stay(root,p,p->cream,(t3_vec3){ax+side*.5f,ay+1.8f,az},(t3_vec3){ax+side*.9f,ay+1.2f,az+.2f},.2f);
    prop(root,p->ball,p->cream,ax+side*.9f,ay+1.15f,az+.2f,.23f,.25f,.22f);
  }
  mb prints={0};
  for(int j=0;j<14;j++) {
    float px=x+1.6f+j*.28f,pz=z+3.8f-j*.13f+(j%2?.18f:-.18f);
    int center=mb_v(&prints,(t3_vec3){px,bank_surface_y(px,pz,0)+.012f,pz},(t3_vec3){0,1,0},0,0,1);
    int rim[12];
    for(int k=0;k<12;k++) {
      float a=k*6.2831853f/12,fx=px+.14f*cw_cos(a),fz=pz+.25f*cw_sin(a);
      rim[k]=mb_v(&prints,(t3_vec3){fx,bank_surface_y(fx,fz,0)+.012f,fz},(t3_vec3){0,1,0},0,0,1);
    }
    for(int k=0;k<12;k++)mb_t(&prints,center,rim[(k+1)%12],rim[k]);
  }
  t3_geometry *pg=mb_done(&prints);prop(root,pg,dust,0,0,0,1,1,1);t3_release(pg);
  /* Dislodged thermal panels tell the crash story without extra simulation. */
  for(int j=0;j<4;j++) {
    float px=x-4-j*.9f,pz=z+1+(j%2);
    t3_mesh *panel=prop(root,p->box,j%2?foil:silver,px,bank_surface_y(px,pz,0)+.15f,pz,.9f,.12f,.7f);
    t3_object_set_rotation(&panel->base,0,j*.7f,.12f);
  }
  t3_release(oct);t3_release(foil);t3_release(silver);t3_release(dust);
}

/* Circus acts sit in small rings set back from the course. Rigid parts are
 * merged by material; only a trunk or head needs an animated transform. */
static void circus_elephant(t3_object *root,const whimsy_parts *p,float x,float z,float scale,int color) {
  t3_material *skin=t3_mesh_standard_material_new(0x8d929c);skin->roughness=.9f;
  t3_material *inner=t3_mesh_standard_material_new(0xb5949b);inner->roughness=1;
  t3_object *body=t3_group_new();body->position=(t3_vec3){x,bank_y(x,z,0),z};body->scale=(t3_vec3){scale,scale,scale};t3_object_add(root,body);
  prop(body,p->ball,skin,0,2.25f,0,1.65f,1.3f,.85f);
  prop(body,p->ball,skin,1.35f,2.9f,0,.83f,.95f,.7f);
  for(int a=-1;a<=1;a+=2)for(int b=-1;b<=1;b+=2) {
    prop(body,p->cyl,skin,a*.95f,.85f,b*.52f,.29f,1.7f,.29f);
    prop(body,p->ball,skin,a*.95f,.2f,b*.52f,.37f,.25f,.36f);
    for(int toe=0;toe<3;toe++)prop(body,p->ball,p->cream,a*.95f+.25f,.15f,b*.52f+(toe-1)*.16f,.08f,.1f,.065f);
  }
  for(int side=-1;side<=1;side+=2) {
    prop(body,p->ball,skin,.65f,2.95f,side*.72f,.7f,1.05f,.18f);
    prop(body,p->ball,inner,.72f,2.95f,side*.88f,.48f,.77f,.035f);
    prop(body,p->ball,p->black,1.73f,3.1f,side*.53f,.075f,.08f,.06f);
    prop(body,p->ball,p->cream,1.76f,3.12f,side*.575f,.018f,.018f,.012f);
    for(int j=0;j<6;j++) {
      float u=j/6.f,v=(j+1)/6.f;
      circus_stay(body,p,p->cream,(t3_vec3){1.8f+u*.9f,2.45f-.4f*cw_sin(u*2.5f),side*.4f},
        (t3_vec3){1.8f+v*.9f,2.45f-.4f*cw_sin(v*2.5f),side*.4f},.11f*(1-u)+.015f);
    }
  }
  t3_material *blanket=color?p->blue:p->red;
  prop(body,p->ball,blanket,-.15f,3.24f,0,1,.25f,.91f);
  for(int side=-1;side<=1;side+=2) {
    prop(body,p->box,blanket,-.15f,2.75f,side*.87f,1.6f,.95f,.055f);
    for(int j=0;j<5;j++)prop(body,p->cone,p->gold,-.75f+j*.3f,2.2f,side*.9f,.09f,.25f,.06f);
  }
  circus_stay(body,p,skin,(t3_vec3){-1.5f,2.3f,0},(t3_vec3){-2,1.35f,.12f},.055f);
  prop(body,p->ball,p->black,-2,1.3f,.12f,.09f,.2f,.09f);
  merge_still(body,0,body->child_count,false);
  t3_object *trunk=t3_group_new();trunk->position=(t3_vec3){1.96f,2.8f,0};t3_object_add(body,trunk);
  for(int j=0;j<12;j++) {
    float u=j/11.f;
    float px=.25f+u*.95f,py=-1.4f*cw_sin(u*3.1415927f);
    prop(trunk,p->ball,skin,px,py,0,.24f-u*.11f,.24f-u*.11f,.24f-u*.11f);
    if(j) {
      float prev=(j-1)/11.f;
      circus_stay(trunk,p,skin,(t3_vec3){.25f+prev*.95f,-1.4f*cw_sin(prev*3.1415927f),0},
        (t3_vec3){px,py,0},.24f-u*.11f);
    }
  }
  prop(trunk,p->ball,color?p->pink:p->gold,1.2f,.42f,0,.44f,.44f,.44f);
  merge_still(trunk,0,trunk->child_count,false);
  if(whimsy_motion_count<96)whimsy_motion_list[whimsy_motion_count++]=(whimsy_motion){.object=trunk,.phase=x,.kind=7};
  t3_release(trunk);t3_release(body);t3_release(skin);t3_release(inner);
}
static void circus_lion(t3_object *root,const whimsy_parts *p,float x,float z,bool seated) {
  t3_material *fur=t3_mesh_standard_material_new(0xc99846);fur->roughness=.95f;
  t3_material *mane=t3_mesh_standard_material_new(0x754126);mane->roughness=1;
  t3_object *body=t3_group_new();body->position=(t3_vec3){x,bank_y(x,z,0)+.55f,z};t3_object_add(root,body);
  prop(body,p->cyl,seated?p->red:p->blue,0,-.3f,0,1.65f,.6f,1.65f);
  prop(body,p->cyl,p->gold,0,.02f,0,1.7f,.08f,1.7f);
  float h=seated?1.45f:1.05f;
  prop(body,p->ball,fur,-.25f,h,0,1.1f,.62f,.48f);
  for(int side=-1;side<=1;side+=2)for(int end=-1;end<=1;end+=2) {
    prop(body,p->cyl,fur,end*.68f,h*.5f,side*.3f,.15f,h,.15f);
    prop(body,p->ball,fur,end*.68f+.14f,.18f,side*.3f,.28f,.17f,.2f);
  }
  for(int j=0;j<10;j++) {
    float a=j*.16f;
    prop(body,p->ball,fur,-1.25f-j*.09f,h+.22f*cw_sin(a*2),-.15f-j*.09f,.06f,.06f,.06f);
    if(j)circus_stay(body,p,fur,(t3_vec3){-1.25f-(j-1)*.09f,h+.22f*cw_sin((a-.16f)*2),-.15f-(j-1)*.09f},
      (t3_vec3){-1.25f-j*.09f,h+.22f*cw_sin(a*2),-.15f-j*.09f},.055f);
  }
  prop(body,p->ball,mane,-2.1f,h+.1f,-1,.13f,.13f,.13f);
  merge_still(body,0,body->child_count,false);
  t3_object *head=t3_group_new();head->position=(t3_vec3){.82f,h+.6f,0};t3_object_add(body,head);
  for(int j=0;j<15;j++) {
    float a=j*6.2831853f/15;
    prop(head,p->ball,mane,-.14f,cw_cos(a)*.55f,cw_sin(a)*.5f,.37f,.3f,.3f);
  }
  prop(head,p->ball,fur,.1f,.05f,0,.56f,.53f,.47f);
  for(int side=-1;side<=1;side+=2) {
    prop(head,p->ball,fur,0,.51f,side*.35f,.17f,.2f,.16f);
    prop(head,p->ball,p->black,.42f,.19f,side*.28f,.055f,.065f,.045f);
    prop(head,p->ball,p->cream,.53f,-.15f,side*.15f,.24f,.18f,.18f);
  }
  prop(head,p->ball,p->black,.71f,-.07f,0,.09f,.07f,.1f);
  merge_still(head,0,head->child_count,false);
  if(whimsy_motion_count<96)whimsy_motion_list[whimsy_motion_count++]=(whimsy_motion){.object=head,.phase=x,.kind=8};
  t3_release(head);t3_release(body);t3_release(fur);t3_release(mane);
}

/* A late-course listening post: one slowly scanning rigid assembly, with
 * shared materials and a small parabolic shell instead of a dense model. */
static void lunar_listening_post(t3_object *root,const whimsy_parts *p,float x,float z) {
  float y=bank_surface_y(x,z,0);
  prop(root,p->cyl,p->black,x,y+1.5f,z,.28f,3,.28f);
  for(int j=0;j<3;j++) {
    float a=j*2.0943951f,fx=x+2*cw_cos(a),fz=z+2*cw_sin(a);
    float fy=bank_surface_y(fx,fz,0);
    circus_stay(root,p,p->cream,(t3_vec3){x,y+2.6f,z},(t3_vec3){fx,fy+.12f,fz},.09f);
    prop(root,p->cyl,p->black,fx,fy+.1f,fz,.45f,.2f,.45f);
  }
  t3_object *dish=t3_group_new();dish->position=(t3_vec3){x,y+4.2f,z};t3_object_add(root,dish);
  mb shell={0};
  for(int side=0;side<2;side++)for(int ring=0;ring<6;ring++)for(int seg=0;seg<32;seg++) {
    int base=0;
    for(int k=0;k<4;k++) {
      float r=(ring+(k==1||k==2))*2.5f/6;
      float a=(seg+(k>=2))*6.2831853f/32;
      float px=r*cw_cos(a),py=r*cw_sin(a);
      t3_vec3 n=t3_vec3_normalize((t3_vec3){-.32f*px,-.32f*py,1});
      if(side)n=t3_vec3_scale(n,-1);
      int v=mb_v(&shell,(t3_vec3){px,py,.16f*r*r-(side?.055f:0)},n,px*.2f,py*.2f,1);
      if(k==0)base=v;
    }
    if(side){mb_t(&shell,base,base+2,base+1);mb_t(&shell,base,base+3,base+2);}
    else {mb_t(&shell,base,base+1,base+2);mb_t(&shell,base,base+2,base+3);}
  }
  t3_geometry *geo=mb_done(&shell);prop(dish,geo,p->cream,0,0,0,1,1,1);t3_release(geo);
  prop(dish,p->ring,p->black,0,0,1,2.5f,2.5f,.3f);
  for(int j=0;j<3;j++) {
    float a=j*2.0943951f;
    circus_stay(dish,p,p->black,(t3_vec3){2.35f*cw_cos(a),2.35f*cw_sin(a),.9f},(t3_vec3){0,0,1.9f},.035f);
  }
  prop(dish,p->cyl,p->gold,0,0,1.9f,.14f,.35f,.14f);
  merge_still(dish,0,dish->child_count,false);
  if(whimsy_motion_count<96)whimsy_motion_list[whimsy_motion_count++]=(whimsy_motion){.object=dish,.kind=9};
  t3_release(dish);
  /* Two low solar wings sit at different depths, leaving the dish silhouette clear. */
  for(int wing=-1;wing<=1;wing+=2) {
    float px=x+wing*4.5f,pz=z-1,py=bank_surface_y(px,pz,0);
    prop(root,p->cyl,p->black,px,py+.8f,pz,.08f,1.6f,.08f);
    t3_object *panel=t3_group_new();panel->position=(t3_vec3){px,py+1.6f,pz};
    t3_object_set_rotation(panel,-.85f,wing*.12f,0);t3_object_add(root,panel);
    prop(panel,p->box,p->cream,0,0,0,3.3f,1.9f,.09f);
    for(int col=0;col<6;col++)for(int row=0;row<3;row++)
      prop(panel,p->box,p->blue,-1.35f+col*.54f,-.59f+row*.59f,.06f,.49f,.54f,.035f);
    merge_still(panel,0,panel->child_count,false);t3_release(panel);
  }
}

static void desert_alien(t3_object *root,const whimsy_parts *p,float x,float z,bool wave) {
  t3_material *skin=t3_mesh_standard_material_new(0x8aa86c);skin->roughness=.75f;
  float y=bank_surface_y(x,z,0);
  t3_object *a=t3_group_new();a->position=(t3_vec3){x,y,z};t3_object_add(root,a);
  prop(a,p->ball,skin,0,1.25f,0,.3f,.58f,.22f);
  prop(a,p->cyl,skin,0,1.85f,0,.09f,.4f,.09f);
  prop(a,p->ball,skin,0,2.24f,0,.48f,.58f,.35f);
  prop(a,p->ball,skin,0,1.99f,.15f,.24f,.27f,.24f);
  for(int side=-1;side<=1;side+=2) {
    t3_mesh *eye=prop(a,p->ball,p->black,side*.21f,2.27f,.295f,.14f,.22f,.065f);
    t3_object_set_rotation(&eye->base,0,side*.3f,-side*.35f);
    prop(a,p->ball,p->cream,side*.23f,2.34f,.35f,.025f,.035f,.016f);
    circus_stay(a,p,skin,(t3_vec3){side*.16f,.95f,0},(t3_vec3){side*.25f,.2f,.07f},.075f);
    prop(a,p->ball,skin,side*.25f,.1f,.15f,.12f,.1f,.25f);
  }
  prop(a,p->box,p->black,0,1.98f,.355f,.16f,.025f,.02f);
  circus_stay(a,p,skin,(t3_vec3){-.25f,1.55f,0},(t3_vec3){-.54f,1.05f,.15f},.065f);
  prop(a,p->box,p->blue,-.52f,1.04f,.25f,.32f,.4f,.08f);
  prop(a,p->box,p->lamp,-.52f,1.06f,.3f,.22f,.24f,.02f);
  merge_still(a,0,a->child_count,false);
  t3_object *arm=t3_group_new();arm->position=(t3_vec3){.28f,1.58f,0};t3_object_add(a,arm);
  circus_stay(arm,p,skin,(t3_vec3){0,0,0},(t3_vec3){.38f,.35f,0},.065f);
  circus_stay(arm,p,skin,(t3_vec3){.38f,.35f,0},(t3_vec3){.32f,.72f,.04f},.055f);
  prop(arm,p->ball,skin,.32f,.78f,.04f,.09f,.13f,.065f);
  for(int j=0;j<3;j++)circus_stay(arm,p,skin,(t3_vec3){.26f+j*.06f,.83f,.04f},(t3_vec3){.22f+j*.1f,1.02f,.04f},.018f);
  merge_still(arm,0,arm->child_count,false);
  if(wave&&whimsy_motion_count<96)whimsy_motion_list[whimsy_motion_count++]=(whimsy_motion){.object=arm,.phase=x,.kind=7};
  t3_release(arm);t3_release(a);t3_release(skin);
}
static void desert_wreck(t3_object *root,const whimsy_parts *p,const track_desc *t) {
  float x=t->finish_x*.56f,z=-11,y=bank_surface_y(x,z,0);
  t3_material *metal=t3_mesh_standard_material_new(0x7e8990);metal->metalness=.8f;metal->roughness=.34f;
  t3_material *scorch=t3_mesh_standard_material_new(0x342b29);scorch->roughness=1;
  t3_material *neon=glow(.18f,.9f,.65f,2.5f);
  /* Scorched landing scar follows the visible sand; no hovering dark disc. */
  mb scar={0};
  for(int ring=0;ring<24;ring++)for(int i=0;i<64;i++) {
    int v[4];
    for(int k=0;k<4;k++) {
      float r=(ring+(k>=2))/24.f,a=(i+(k==1||k==2))*6.2831853f/64;
      float px=x+cw_cos(a)*7.4f*r,pz=z+cw_sin(a)*5.6f*r;
      v[k]=mb_v(&scar,(t3_vec3){px,bank_surface_y(px,pz,0)+.08f,pz},(t3_vec3){0,1,0},0,0,1);
    }
    mb_t(&scar,v[0],v[1],v[2]);mb_t(&scar,v[0],v[2],v[3]);
  }
  t3_geometry *sg=mb_done(&scar);add(root,sg,scorch,false);t3_release(sg);
  t3_object *ship=t3_group_new();ship->position=(t3_vec3){x,y+1.1f,z};t3_object_set_rotation(ship,.12f,.2f,-.22f);t3_object_add(root,ship);
  prop(ship,p->ball,metal,0,.2f,0,5.8f,.8f,4.5f);
  prop(ship,p->ball,p->black,0,-.14f,0,5.5f,.45f,4.2f);
  prop(ship,p->ball,p->glass,0,.95f,0,2.1f,1.4f,1.8f);
  for(int i=0;i<18;i++) {
    float a=i*6.2831853f/18;
    prop(ship,p->ball,i%4==0?p->black:neon,cw_cos(a)*5.5f,.18f,cw_sin(a)*4.3f,.22f,.12f,.22f);
  }
  prop(ship,p->box,scorch,-1.1f,.85f,.5f,1.3f,.16f,1.1f);
  t3_mesh *hatch=prop(ship,p->box,metal,-1.6f,1.45f,.5f,.12f,1.3f,1.1f);t3_object_set_rotation(&hatch->base,0,0,-.4f);
  merge_still(ship,0,ship->child_count,false);t3_release(ship);
  /* Broken antenna and panels trace the skid toward the wreck. */
  for(int i=0;i<5;i++) {
    float px=x-9-i*1.8f,pz=z+.7f*cw_sin(i*2);
    t3_mesh *panel=prop(root,p->box,metal,px,bank_surface_y(px,pz,0)+.12f,pz,.7f+i*.13f,.16f,.5f);
    t3_object_set_rotation(&panel->base,.1f,i*.8f,.12f);
  }
  desert_alien(root,p,x+8,-7.5f,true);
  desert_alien(root,p,t->finish_x*.81f,-7,true);
  float sx=t->finish_x*.81f+3,sz=-9,sy=bank_surface_y(sx,sz,0);
  for(int j=0;j<3;j++) {
    float a=j*2.094395f;
    circus_stay(root,p,metal,(t3_vec3){sx,sy+1.7f,sz},(t3_vec3){sx+cw_cos(a)*1.1f,sy,sz+cw_sin(a)*1.1f},.055f);
  }
  prop(root,p->ball,p->glass,sx,sy+2.1f,sz,.55f,.5f,.55f);
  whimsy_bob(prop(root,p->ball,neon,sx,sy+3,sz,.25f,.25f,.25f),1);
  t3_release(metal);t3_release(scorch);t3_release(neon);
}

/* Autumn discoveries: a quiet animal encounter, then a moving rural landmark. */
static void autumn_discoveries(t3_object *root,const whimsy_parts *p,const track_desc *t) {
  t3_material *fur=t3_mesh_standard_material_new(0xb9511c);fur->roughness=1;
  t3_material *ivory=t3_mesh_standard_material_new(0xe4d4ac);ivory->roughness=1;
  t3_material *dark=t3_mesh_standard_material_new(0x29211d);dark->roughness=.95f;
  t3_material *wood=mat(TEX_WOOD,(float[3]){.58f,.39f,.23f},2,1,.5f,.95f);
  float x=t->finish_x*.54f,z=-5.8f,y=bank_surface_y(x,z,0);
  float low=y,high=y;
  for(int ix=-1;ix<=1;ix++)for(int iz=-1;iz<=1;iz++) {
    float h=bank_surface_y(x+ix*1.6f,z+iz*1.1f,0);low=fminf(low,h);high=fmaxf(high,h);
  }
  t3_material *shelf=mat(TEX_MOSSROCK,(float[3]){.7f,.65f,.52f},1,1,.5f,1);
  carved_stone(root,shelf,x,(low+high)*.5f-.1f,z,4.2f,high-low+.6f,3.1f);
  t3_release(shelf);y=high+.24f;
  /* A curled fox: bushy tail wraps the body, white tip beneath a pointed muzzle. */
  prop(root,p->ball,fur,x,y+.57f,z,1.35f,.6f,.8f);
  for(int i=0;i<8;i++) {
    float a=.25f+i*.34f,r=1.18f;
    prop(root,p->ball,i>5?ivory:fur,x+cw_cos(a)*r,y+.3f,z+cw_sin(a)*.7f,.46f,.3f,.36f);
  }
  prop(root,p->ball,fur,x-.8f,y+.65f,z+.6f,.62f,.45f,.48f);
  prop(root,p->ball,ivory,x-1.13f,y+.48f,z+.92f,.4f,.22f,.28f);
  prop(root,p->ball,dark,x-1.4f,y+.49f,z+1.03f,.12f,.1f,.1f);
  for(int side=-1;side<=1;side+=2) {
    prop(root,p->cone,fur,x-.8f+side*.3f,y+1.15f,z+.48f,.25f,.65f,.21f);
    prop(root,p->cone,dark,x-.8f+side*.3f,y+1.16f,z+.61f,.13f,.4f,.06f);
  }
  t3_mesh *eye=prop(root,p->box,dark,x-.86f,y+.74f,z+1.035f,.26f,.045f,.035f);
  t3_object_set_rotation(&eye->base,0,0,-.13f);
  /* Fallen log rests behind the fox; rough bark and a pale cut end. */
  float lx=x+1.2f,lz=z-1.7f,ly=bank_surface_y(lx,lz,0)+.4f;
  t3_mesh *log=prop(root,p->cyl,wood,lx,ly,lz,.5f,4.2f,.5f);t3_object_set_rotation(&log->base,0,0,1.5707963f);
  t3_mesh *end=prop(root,p->cyl,ivory,lx-2.11f,ly,lz,.4f,.035f,.4f);t3_object_set_rotation(&end->base,0,0,1.5707963f);
  x=t->finish_x*.81f;z=-8;y=bank_surface_y(x,z,0);
  t3_material *plaster=mat(TEX_CONCRETE,(float[3]){.88f,.78f,.58f},2,2,.4f,.95f);
  prop(root,p->cyl,plaster,x,y+1.5f,z,1.15f,3.1f,1.15f);
  prop(root,p->cone,wood,x,y+3.5f,z,1.45f,1.4f,1.45f);
  prop(root,p->box,dark,x,y+.65f,z+1.12f,.65f,1.3f,.1f);
  prop(root,p->box,dark,x+.62f,y+2.2f,z+1.02f,.36f,.55f,.1f);
  t3_object *sails=t3_group_new();sails->position=(t3_vec3){x,y+3.05f,z+1.6f};t3_object_add(root,sails);
  for(int i=0;i<4;i++) {
    float a=i*1.5707963f,cs=cw_cos(a),sn=cw_sin(a);
    for(int j=0;j<7;j++) {
      float px=j==0?0:.23f,py=j==0?.95f:j==1?1.35f:.78f+(j-2)*.28f;
      t3_mesh *piece=prop(sails,p->box,j==1?ivory:wood,px*cs-py*sn,px*sn+py*cs,j==0?0:j==1?.03f:.075f,
        j==0?.1f:j==1?.52f:.61f,j==0?2.05f:j==1?1.25f:.035f,j==0?.1f:j==1?.055f:.04f);
      t3_object_set_rotation(&piece->base,0,0,a);
    }
  }
  prop(sails,p->ball,wood,0,0,.1f,.22f,.22f,.18f);
  merge_still(sails,0,sails->child_count,false);
  if(whimsy_motion_count<96)whimsy_motion_list[whimsy_motion_count++]=(whimsy_motion){.object=sails,.kind=11};
  t3_release(sails);
  for(int i=0;i<4;i++) {
    float px=x+3+i*1.1f,pz=z-2-(i%2),py=bank_surface_y(px,pz,0);
    prop(root,p->box,wood,px,py+.4f,pz,.8f,.8f,.75f);
  }
  t3_release(fur);t3_release(ivory);t3_release(dark);t3_release(wood);t3_release(plaster);
}

/* An abandoned dig, followed by the animal the miners unearthed. */
static void fossil_bone(t3_object *root,const whimsy_parts *p,t3_material *m,
                        t3_vec3 a,t3_vec3 b,float radius) {
  float dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z;
  float length=sqrtf(dx*dx+dy*dy+dz*dz);if(length<.001f)return;
  t3_mesh *bone=prop(root,p->cyl,m,(a.x+b.x)*.5f,(a.y+b.y)*.5f,(a.z+b.z)*.5f,radius,length,radius);
  float qx=dz/length,qz=-dx/length,qw=1+dy/length;
  float norm=sqrtf(qx*qx+qz*qz+qw*qw);
  bone->base.quaternion=norm>.001f?(t3_quat){qx/norm,0,qz/norm,qw/norm}:(t3_quat){1,0,0,0};
}
static void canyon_discoveries(t3_object *root,const whimsy_parts *p,const track_desc *t) {
  t3_material *ivory=t3_mesh_standard_material_new(0xcfc0a0);ivory->roughness=.96f;
  t3_material *iron=t3_mesh_standard_material_new(0x493e39);iron->roughness=.78f;
  t3_material *wood=mat(TEX_WOOD,(float[3]){.53f,.35f,.22f},2,1,.5f,.95f);
  t3_material *stone=mat(TEX_SANDSTONE,(float[3]){.72f,.48f,.3f},1,1,.5f,1);
  float x=t->finish_x*.49f,z=-4.8f,y=bank_surface_y(x,z,0);
  float low=y,high=y;
  for(int i=-1;i<=1;i++)for(int j=-1;j<=1;j++) {
    float h=bank_surface_y(x+i*2.6f,z+j*.9f,0);low=fminf(low,h);high=fmaxf(high,h);
  }
  carved_stone(root,stone,x,(low+high)*.5f-.2f,z,6.6f,high-low+.35f,2.5f);y=high+.12f;
  for(int rail=-1;rail<=1;rail+=2)prop(root,p->box,iron,x,y+.13f,z+rail*.65f,6,.12f,.12f);
  for(int i=0;i<9;i++)prop(root,p->box,wood,x-2.7f+i*.66f,y,z,.2f,.12f,1.85f);
  /* Open-top cart: individual planks, visible wheels, angular mineral cargo. */
  prop(root,p->box,wood,x,y+.57f,z,2.5f,.18f,1.6f);
  for(int row=0;row<3;row++) {
    for(int side=-1;side<=1;side+=2) {
      prop(root,p->box,wood,x,y+.77f+row*.25f,z+side*.8f,2.6f,.22f,.12f);
      prop(root,p->box,wood,x+side*1.25f,y+.77f+row*.25f,z,.12f,.22f,1.6f);
    }
  }
  for(int side=-1;side<=1;side+=2)for(int axle=-1;axle<=1;axle+=2) {
    t3_mesh *wheel=prop(root,p->cyl,iron,x+axle*.8f,y+.35f,z+side*.83f,.32f,.15f,.32f);
    t3_object_set_rotation(&wheel->base,1.5707963f,0,0);
    prop(root,p->ball,p->black,x+axle*.8f,y+.35f,z+side*.93f,.12f,.12f,.05f);
  }
  for(int i=0;i<7;i++) {
    float xx=x-.9f+i*.28f,zz=z+.3f*cw_sin(i*2.4f);
    t3_mesh *crystal=prop(root,p->cone,i%2?p->glass:p->gold,xx,y+1.4f+(i%3)*.12f,zz,.25f,.8f+(i%3)*.18f,.25f);
    t3_object_set_rotation(&crystal->base,0,0,(i-3)*.12f);
  }
  /* The fossil follows the real bank, with ribs rising from the buried spine. */
  x=t->finish_x*.78f;z=-7;
  for(int i=0;i<15;i++) {
    float xx=x-5+i*.65f,zz=z+.22f*cw_sin(i*.45f),yy=bank_surface_y(xx,zz,0)+.22f;
    prop(root,p->ball,ivory,xx,yy,zz,.38f,.27f,.34f);
    if(i>2&&i<12)for(int side=-1;side<=1;side+=2) {
      float width=1.5f*cw_sin((i-1)*.25f),height=1.7f*cw_sin((i-1)*.25f);
      t3_vec3 prev={xx,yy,zz};
      for(int k=1;k<=6;k++) {
        float a=k*3.1415927f/7;
        t3_vec3 next={xx-.12f*k,yy+height*cw_sin(a),zz+side*width*(1-cw_cos(a))*.65f};
        fossil_bone(root,p,ivory,prev,next,.085f);prev=next;
      }
    }
  }
  float sx=x+5.5f,sy=bank_surface_y(sx,z,0)+.45f;
  /* Long reptile skull, deep sockets and separate tooth-lined jaws. */
  prop(root,p->ball,ivory,sx,sy+.25f,z,1.05f,.55f,.65f);
  prop(root,p->box,ivory,sx+1,sy+.18f,z,1.6f,.3f,.8f);
  prop(root,p->box,ivory,sx+.8f,sy-.25f,z,1.9f,.18f,.78f);
  for(int side=-1;side<=1;side+=2) {
    prop(root,p->ball,p->black,sx+.05f,sy+.43f,z+side*.56f,.32f,.28f,.08f);
    for(int i=0;i<7;i++) {
      t3_mesh *tooth=prop(root,p->cone,ivory,sx+.15f+i*.24f,sy-.04f,z+side*.36f,.075f,.27f,.075f);
      t3_object_set_rotation(&tooth->base,0,0,3.1415927f);
    }
  }
  t3_release(ivory);t3_release(iron);t3_release(wood);t3_release(stone);
}

static void coast_discoveries(t3_object *root,const whimsy_parts *p,const track_desc *t) {
  t3_material *plaster=mat(TEX_CONCRETE,(float[3]){.88f,.86f,.76f},2,2,.35f,.93f);
  t3_material *red=t3_mesh_standard_material_new(0x9d302a);red->roughness=.8f;
  t3_material *wood=mat(TEX_WOOD,(float[3]){.43f,.31f,.23f},2,1,.65f,.92f);
  t3_material *stone=mat(TEX_GRANITE,(float[3]){.48f,.51f,.5f},1,1,.6f,.95f);
  float x=t->finish_x*.55f,z=-5.5f,y=bank_surface_y(x,z,1),low=y,high=y;
  for(int i=-1;i<=1;i++)for(int j=-1;j<=1;j++) {
    float h=bank_surface_y(x+i*1.4f,z+j*1.4f,1);low=fminf(low,h);high=fmaxf(high,h);
  }
  carved_stone(root,stone,x,(low+high)*.5f-.12f,z,3.4f,high-low+.3f,3.4f);y=high;
  /* Broad foot, tapered striped tower and an open lantern gallery. */
  for(int i=0;i<6;i++) {
    t3_geometry *g=t3_cylinder_geometry_new(1.05f-i*.075f,1.125f-i*.075f,.6f,16,1,false);
    prop(root,g,i%2?red:plaster,x,y+.3f+i*.6f,z,1,1,1);t3_release(g);
  }
  prop(root,p->box,p->black,x,y+.65f,z+1.06f,.52f,1.3f,.08f);
  prop(root,p->box,p->black,x,y+2.15f,z+.88f,.3f,.45f,.08f);
  prop(root,p->cyl,p->black,x,y+3.65f,z,1.05f,.15f,1.05f);
  for(int i=0;i<12;i++) {
    float a=i*6.2831853f/12;
    prop(root,p->cyl,p->black,x+.95f*cw_cos(a),y+3.97f,z+.95f*cw_sin(a),.025f,.55f,.025f);
  }
  t3_mesh *rail=prop(root,p->ring,p->black,x,y+4.22f,z,.95f,.95f,.2f);
  t3_object_set_rotation(&rail->base,1.5707963f,0,0);
  for(int i=0;i<6;i++) {
    float a=i*6.2831853f/6;
    prop(root,p->cyl,p->black,x+.63f*cw_cos(a),y+4.2f,z+.63f*cw_sin(a),.035f,.95f,.035f);
  }
  prop(root,p->cone,red,x,y+4.9f,z,.94f,.6f,.94f);
  prop(root,p->ball,p->gold,x,y+5.25f,z,.12f,.17f,.12f);
  t3_object *beacon=t3_group_new();beacon->position=(t3_vec3){x,y+4.2f,z};t3_object_add(root,beacon);
  prop(beacon,p->box,p->black,0,0,0,.45f,.55f,.3f);
  prop(beacon,p->ball,p->lamp,0,0,.23f,.25f,.25f,.12f);
  prop(beacon,p->ball,p->lamp,0,0,-.23f,.25f,.25f,.12f);
  merge_still(beacon,0,beacon->child_count,false);
  if(whimsy_motion_count<96)whimsy_motion_list[whimsy_motion_count++]=(whimsy_motion){.object=beacon,.kind=12};
  t3_release(beacon);
  /* A few birds circle the lantern; one transform animates the whole flock. */
  t3_object *flock=t3_group_new();flock->position=(t3_vec3){x,y+4.3f,z};t3_object_add(root,flock);
  for(int i=0;i<3;i++) {
    float a=i*2.1f,bx=3.3f*cw_cos(a),bz=3.3f*cw_sin(a),by=i*.35f;
    prop(flock,p->ball,plaster,bx,by,bz,.12f,.12f,.3f);
    for(int side=-1;side<=1;side+=2) {
      t3_mesh *wing=prop(flock,p->box,plaster,bx+side*.35f,by+.1f,bz,.6f,.045f,.18f);
      t3_object_set_rotation(&wing->base,0,0,side*.28f);
      prop(flock,p->ball,p->black,bx+side*.63f,by+.18f,bz,.12f,.035f,.13f);
    }
  }
  merge_still(flock,0,flock->child_count,false);
  if(whimsy_motion_count<96)whimsy_motion_list[whimsy_motion_count++]=(whimsy_motion){.object=flock,.kind=12,.phase=1};
  t3_release(flock);
  /* A storm-broken wooden boat farther along, with ribs and a snapped mast. */
  x=t->finish_x*.82f;z=-6;y=bank_surface_y(x,z,1);
  t3_object *boat=t3_group_new();boat->position=(t3_vec3){x,y-.15f,z};t3_object_set_rotation(boat,.16f,-.18f,-.09f);t3_object_add(root,boat);
  prop(boat,p->box,wood,0,0,0,6.8f,.2f,.22f);
  for(int i=0;i<9;i++) {
    float bx=-3.2f+i*.8f,w=.18f+1.2f*cw_sin((i+.5f)*3.1415927f/9);
    for(int side=-1;side<=1;side+=2) {
      fossil_bone(boat,p,wood,(t3_vec3){bx,0,0},(t3_vec3){bx,1.1f,side*w},.085f);

    }
  }
  mb hull={0};
  for(int side=-1;side<=1;side+=2)for(int i=0;i<8;i++)for(int row=0;row<5;row++) {
    if((i==5&&row>2)||(i==1&&row==4))continue;
    float bx=-3.2f+i*.8f,w=.18f+1.2f*cw_sin((i+.5f)*3.1415927f/9);
    float nw=.18f+1.2f*cw_sin((i+1.5f)*3.1415927f/9);
    for(int inner=0;inner<2;inner++) {
    int first=hull.nv;
    for(int v=0;v<4;v++) {
      bool right=v==1||v==2,top=v>=2;
      float angle=.15f+(row+(top?.94f:.02f))*.27f;
      float xx=bx+(right?.8f:0),zz=side*(right?nw:w)*cw_sin(angle),yy=1.3f*(1-cw_cos(angle));
      float sign=inner?-1:1;
      mb_v(&hull,(t3_vec3){xx,yy+inner*.045f,zz-side*inner*.045f},(t3_vec3){0,-sign*cw_cos(angle),sign*side*cw_sin(angle)},xx*.8f,row*.25f+(top?.23f:0),1);
    }
    if((side>0)!=inner){mb_t(&hull,first,first+1,first+2);mb_t(&hull,first,first+2,first+3);}
    else {mb_t(&hull,first,first+2,first+1);mb_t(&hull,first,first+3,first+2);}
    }
  }
  t3_geometry *hg=mb_done(&hull);
  t3_material *hullwood=mat(TEX_WOOD,(float[3]){.5f,.34f,.21f},1,1,.5f,.9f);
  prop(boat,hg,hullwood,0,0,0,1,1,1);t3_release(hg);t3_release(hullwood);
  fossil_bone(boat,p,wood,(t3_vec3){-.6f,0,0},(t3_vec3){-.9f,2.3f,.1f},.12f);
  fossil_bone(boat,p,wood,(t3_vec3){-.9f,2.3f,.1f},(t3_vec3){2.1f,1,.5f},.09f);
  prop(boat,p->ring,red,1.5f,.85f,1.1f,.35f,.35f,.7f);
  merge_still(boat,0,boat->child_count,false);t3_release(boat);
  t3_release(plaster);t3_release(red);t3_release(wood);t3_release(stone);
}

/* Mechanical astronomy among the floating rocks, then a stranded observer. */
static void cosmic_discoveries(t3_object *root,const whimsy_parts *p,const track_desc *t) {
  t3_material *metal=t3_mesh_standard_material_new(0x8d9eae);metal->metalness=.7f;metal->roughness=.35f;
  t3_material *violet=glow(.65f,.2f,1,1.5f),*cyan=glow(.12f,.7f,1,1.5f);
  float x=t->finish_x*.47f,z=-4.2f,y=bank_surface_y(x,z,1);
  prop(root,p->cyl,p->black,x,y+.25f,z,1.4f,.5f,1.4f);
  prop(root,p->cyl,metal,x,y+1.1f,z,.22f,1.7f,.22f);
  float cy=y+2.4f;
  t3_geometry *orbit_ring=t3_torus_geometry_new(1,.025f,6,40,6.2831853f);
  prop(root,p->ball,p->lamp,x,cy,z,.85f,.85f,.85f);
  for(int ring=0;ring<3;ring++) {
    float radius=1.4f+ring*.35f;
    t3_object *plane=t3_group_new();plane->position=(t3_vec3){x,cy,z};
    t3_object_set_rotation(plane,.35f+ring*.55f,.25f-ring*.3f,ring*.7f);t3_object_add(root,plane);
    prop(plane,orbit_ring,ring==1?violet:cyan,0,0,0,radius,radius,radius);
    t3_object *orbit=t3_group_new();t3_object_add(plane,orbit);
    prop(orbit,p->ball,ring==0?p->blue:ring==1?p->pink:p->gold,radius,0,0,.35f+ring*.1f,.35f+ring*.1f,.35f+ring*.1f);
    prop(orbit,p->ball,p->cream,-radius,0,0,.13f,.13f,.13f);
    merge_still(orbit,0,orbit->child_count,false);
    if(whimsy_motion_count<96)whimsy_motion_list[whimsy_motion_count++]=(whimsy_motion){.object=orbit,.kind=13,.phase=(float)ring};
    t3_release(orbit);t3_release(plane);
  }
  t3_release(orbit_ring);
  /* A brass service robot watches the sky beside its collapsed exploration pod. */
  x=t->finish_x*.75f;z=-4.2f;y=bank_surface_y(x,z,1);
  float pod_y=bank_surface_y(x+3,z-1.8f,1);
  prop(root,p->ball,metal,x+3,pod_y+.8f,z-1.8f,1.8f,.8f,1.2f);
  prop(root,p->ball,p->black,x+3,pod_y+1,z-.9f,1.05f,.45f,.15f);
  for(int i=0;i<3;i++)prop(root,p->cone,p->black,x+2+i*.8f,pod_y+.2f,z-1,.28f,.8f,.28f);
  for(int side=-1;side<=1;side+=2) {
    float fx=x+side*.46f,fy=bank_surface_y(fx,z+.12f,1);
    prop(root,p->box,p->black,fx,fy+.16f,z+.2f,.65f,.32f,.85f);
    fossil_bone(root,p,metal,(t3_vec3){fx,fy+.3f,z},(t3_vec3){x+side*.35f,y+1.2f,z},.14f);
    prop(root,p->ball,p->gold,x+side*.35f,y+1.2f,z,.22f,.22f,.22f);
  }
  prop(root,p->box,p->gold,x,y+1.75f,z,1.05f,1.15f,.65f);
  prop(root,p->box,p->black,x,y+1.75f,z+.34f,.65f,.65f,.045f);
  for(int i=0;i<3;i++)prop(root,p->ball,i==0?cyan:violet,x-.2f+i*.2f,y+1.86f,z+.4f,.055f,.055f,.04f);
  prop(root,p->cyl,metal,x,y+2.5f,z,.12f,.4f,.12f);
  t3_object *head=t3_group_new();head->position=(t3_vec3){x,y+2.9f,z};t3_object_add(root,head);
  prop(head,p->box,metal,0,0,0,1.25f,.65f,.75f);
  for(int side=-1;side<=1;side+=2) {
    prop(head,p->ball,p->black,side*.3f,.02f,.4f,.24f,.24f,.12f);
    prop(head,p->ball,cyan,side*.3f,.02f,.5f,.12f,.12f,.045f);
  }
  prop(head,p->cyl,metal,0,.56f,0,.025f,.5f,.025f);
  prop(head,p->ball,violet,0,.85f,0,.1f,.1f,.1f);
  merge_still(head,0,head->child_count,false);
  if(whimsy_motion_count<96)whimsy_motion_list[whimsy_motion_count++]=(whimsy_motion){.object=head,.kind=10,.x=x};
  t3_release(head);
  fossil_bone(root,p,metal,(t3_vec3){x-.65f,y+2.1f,z},(t3_vec3){x-1,y+1.2f,z+.1f},.12f);
  t3_object *arm=t3_group_new();arm->position=(t3_vec3){x+.62f,y+2.1f,z};t3_object_add(root,arm);
  prop(arm,p->cyl,metal,0,.45f,0,.12f,.9f,.12f);
  prop(arm,p->ball,p->gold,0,1,0,.22f,.24f,.15f);
  for(int i=0;i<3;i++)prop(arm,p->box,metal,-.15f+i*.15f,1.3f,0,.08f,.4f,.1f);
  merge_still(arm,0,arm->child_count,false);
  if(whimsy_motion_count<96)whimsy_motion_list[whimsy_motion_count++]=(whimsy_motion){.object=arm,.wave=true};
  t3_release(arm);t3_release(metal);t3_release(violet);t3_release(cyan);
}

static void beach_discoveries(t3_object *root,const whimsy_parts *p,const track_desc *t) {
  t3_material *sand=mat(TEX_SAND,(float[3]){1,.86f,.62f},1,1,.45f,.98f);
  t3_material *wood=mat(TEX_WOOD,(float[3]){.45f,.24f,.1f},1,1,.65f,.85f);
  float x=t->finish_x*.48f,z=-5.2f,y=bank_surface_y(x,z,0);
  /* Unequal towers and a recessed arched entrance make a sand sculpture, not
   * a row of identical buckets. Each tower seats on the actual bank. */
  prop(root,p->box,sand,x,y+.8f,z,4.4f,1.6f,2.2f);
  prop(root,p->box,p->brown,x,y+.55f,z+1.115f,.75f,1.1f,.025f);
  prop(root,p->ball,p->brown,x,y+1.1f,z+1.12f,.375f,.375f,.025f);
  for(int side=-1;side<=1;side+=2)for(int back=0;back<2;back++) {
    float tx=x+side*2.2f,tz=z+(back?-1.3f:1.2f);
    float ty=bank_surface_y(tx,tz,0),h=back?3.1f:2.1f;
    prop(root,p->cyl,sand,tx,ty+h*.5f,tz,.72f,h,.72f);
    prop(root,p->cyl,sand,tx,ty+h,tz,.82f,.22f,.82f);
    for(int i=0;i<8;i++) {
      float a=i*.7853982f;
      prop(root,p->box,sand,tx+.65f*cw_cos(a),ty+h+.28f,tz+.65f*cw_sin(a),.32f,.45f,.32f);
    }
    if(back) {
      prop(root,p->cyl,p->brown,tx,ty+h+.85f,tz,.035f,1.6f,.035f);
      t3_geometry *g=t3_cone_geometry_new(1,1,3,1,false);
      t3_mesh *flag=prop(root,g,side<0?p->red:p->blue,tx+.3f,ty+h+1.35f,tz,.28f,.65f,.04f);
      t3_object_set_rotation(&flag->base,0,0,-1.5707963f);t3_release(g);
    }
  }
  for(int i=0;i<7;i++)prop(root,p->box,sand,x-1.8f+i*.6f,y+1.8f,z+1,.32f,.4f,.4f);
  float bx=x+4,bz=z+1,by=bank_surface_y(bx,bz,0);
  prop(root,p->cyl,p->blue,bx,by+.34f,bz,.4f,.68f,.4f);
  t3_mesh *handle=prop(root,p->ring,p->cream,bx,by+.7f,bz,.38f,.38f,.18f);
  t3_object_set_rotation(&handle->base,0,.3f,0);
  fossil_bone(root,p,p->red,(t3_vec3){bx+.7f,by+.1f,bz},(t3_vec3){bx+1,by+1.1f,bz},.035f);
  prop(root,p->box,p->red,bx+.7f,by+.15f,bz,.3f,.3f,.06f);

  x=t->finish_x*.8f;z=-5;y=bank_surface_y(x,z,0);
  /* Raised lid, brass bands and coins; open interior remains visible. */
  prop(root,p->box,wood,x,y+.13f,z,2.4f,.26f,1.5f);
  for(int side=-1;side<=1;side+=2) {
    prop(root,p->box,wood,x+side*1.12f,y+.55f,z,.16f,.85f,1.5f);
    prop(root,p->box,wood,x,y+.5f,z+side*.68f,2.4f,.7f,.14f);
    prop(root,p->box,p->gold,x+side*.8f,y+.5f,z+.76f,.13f,.75f,.06f);
  }
  t3_mesh *lid=prop(root,p->box,wood,x,y+1.35f,z-.85f,2.4f,1.4f,.14f);
  t3_object_set_rotation(&lid->base,-.3f,0,0);
  for(int i=0;i<16;i++) {
    float cx=x-.8f+(i%4)*.52f,cz=z-.4f+(i/4)*.25f;
    prop(root,p->cyl,p->gold,cx,y+.45f+.05f*(i%3),cz,.18f,.08f,.18f);
  }
  /* Two crabs at different depths and sizes; raised pincers sway. */
  for(int c=0;c<2;c++) {
    float cx=x+(c?3.1f:-2.4f),cz=z+(c?1.1f:-.4f),cy=bank_surface_y(cx,cz,0),s=c?1:.65f;
    t3_object *crab=t3_group_new();crab->position=(t3_vec3){cx,cy,cz};crab->scale=(t3_vec3){s,s,s};t3_object_add(root,crab);
    prop(crab,p->ball,p->red,0,.55f,0,.85f,.38f,.5f);
    for(int side=-1;side<=1;side+=2) {
      fossil_bone(crab,p,p->red,(t3_vec3){side*.3f,.7f,.2f},(t3_vec3){side*.35f,1.1f,.25f},.065f);
      prop(crab,p->ball,p->cream,side*.35f,1.13f,.25f,.14f,.16f,.14f);
      prop(crab,p->ball,p->black,side*.35f,1.15f,.37f,.075f,.09f,.045f);
      for(int leg=0;leg<3;leg++) {
        float lz=-.35f+leg*.35f;
        fossil_bone(crab,p,p->red,(t3_vec3){side*.65f,.5f,lz},(t3_vec3){side*1.15f,.35f,lz+.1f},.06f);
        float foot_y=(bank_surface_y(cx+side*1.4f*s,cz+(lz+.2f)*s,0)-cy)/s+.03f;
        fossil_bone(crab,p,p->red,(t3_vec3){side*1.15f,.35f,lz+.1f},(t3_vec3){side*1.4f,foot_y,lz+.2f},.045f);
      }
      fossil_bone(crab,p,p->red,(t3_vec3){side*.65f,.6f,.25f},(t3_vec3){side*1.1f,1,.55f},.11f);
      t3_object *claw=t3_group_new();claw->position=(t3_vec3){side*1.1f,1,.55f};t3_object_add(crab,claw);
      for(int jaw=-1;jaw<=1;jaw+=2) {
        t3_mesh *finger=prop(claw,p->ball,p->red,jaw*.16f,.22f,0,.15f,.35f,.17f);
        t3_object_set_rotation(&finger->base,0,0,-jaw*.35f);
      }
      merge_still(claw,0,claw->child_count,false);
      if(whimsy_motion_count<96)whimsy_motion_list[whimsy_motion_count++]=(whimsy_motion){.object=claw,.kind=7,.phase=(float)(c*2+side)};
      t3_release(claw);
    }
    /* Child claw groups must remain independent of the static mesh merge. */
    merge_still(crab,0,crab->child_count,false);t3_release(crab);
  }
  t3_release(sand);t3_release(wood);
}

static void scary_discoveries(t3_object *root,const whimsy_parts *p,const track_desc *t) {
  t3_material *sheet=t3_mesh_standard_material_new(0xd4e4d5);sheet->roughness=.85f;
  t3_material *silk=t3_mesh_standard_material_new(0xaab9b4);silk->roughness=.6f;
  t3_material *shell=t3_mesh_standard_material_new(0x302033);shell->roughness=.42f;
  t3_material *eyes=glow(.7f,.12f,.06f,2);
  /* Rounded heads flow into fluted sheet hems, rather than floating spheres. */
  mb cloth={0};
  for(int row=0;row<5;row++)for(int k=0;k<32;k++) {
    float a=k*6.2831853f/32,f=row/4.f;
    float radius=.52f+(.18f+.07f*cw_cos(a*7))*f;
    mb_v(&cloth,(t3_vec3){radius*cw_cos(a),1-f+(row==4?.12f*cw_cos(a*7):0),radius*cw_sin(a)},
         (t3_vec3){cw_cos(a),.18f,cw_sin(a)},k/32.f,f,1);
  }
  for(int row=0;row<4;row++)for(int k=0;k<32;k++) {
    int a=row*32+k,b=row*32+(k+1)%32,c=a+32,d=b+32;
    mb_t(&cloth,a,b,c);mb_t(&cloth,b,d,c);
  }
  t3_geometry *drape=mb_done(&cloth);
  float gx=t->finish_x*.48f,gz=-5.8f;
  float gy=bank_surface_y(gx,gz,0);
  prop(root,p->box,p->brown,gx,gy+.15f,gz,2.7f,.3f,1.5f);
  prop(root,p->box,p->black,gx,gy+.31f,gz,2.35f,.025f,1.2f);
  t3_mesh *lid=prop(root,p->box,p->brown,gx,gy+.55f,gz-.9f,2.7f,.16f,1.5f);
  t3_object_set_rotation(&lid->base,-.9f,0,0);
  for(int i=0;i<3;i++) {
    float x=gx+(i-1)*2.3f,z=gz-(i==1?1.7f:0),y=bank_surface_y(x,z,0)+1.3f;
    t3_object *g=t3_group_new();g->position=(t3_vec3){x,y,z};t3_object_add(root,g);
    float sc=i==0?1.15f:i==1?.7f:.5f;g->scale=(t3_vec3){sc,sc,sc};
    prop(g,p->ball,sheet,0,1,0,.52f,.62f,.52f);
    prop(g,drape,sheet,0,0,0,1,1,1);
    for(int side=-1;side<=1;side+=2) {
      prop(g,p->ball,p->black,side*.19f,1.13f,.48f,.095f,.15f,.04f);
      t3_mesh *arm=prop(g,p->ball,sheet,side*.65f,.8f,0,.38f,.17f,.2f);
      t3_object_set_rotation(&arm->base,0,0,side*.35f);
    }
    prop(g,p->ball,p->black,0,.81f,.54f,.1f,.16f,.025f);
    merge_still(g,0,g->child_count,false);
    if(whimsy_motion_count<96)whimsy_motion_list[whimsy_motion_count++]=(whimsy_motion){.object=g,.y=y,.phase=i*2};
    t3_release(g);
  }
  t3_release(drape);
  /* A web stretches between crooked timber uprights, later than the ghosts. */
  float x=t->finish_x*.85f,z=-5.5f,y=bank_surface_y(x,z,0),cy=y+3;
  for(int side=-1;side<=1;side+=2) {
    float px=x+side*3,py=bank_surface_y(px,z,0);
    fossil_bone(root,p,p->brown,(t3_vec3){px,py,z},(t3_vec3){px-side*.25f,cy+2.4f,z},.14f);
  }
  for(int spoke=0;spoke<12;spoke++) {
    float a=spoke*6.2831853f/12;
    fossil_bone(root,p,silk,(t3_vec3){x,cy,z},(t3_vec3){x+2.7f*cw_cos(a),cy+2.2f*cw_sin(a),z},.012f);
    for(int ring=1;ring<=5;ring++) {
      float b=(spoke+1)*6.2831853f/12,r=ring/5.f;
      fossil_bone(root,p,silk,(t3_vec3){x+2.7f*r*cw_cos(a),cy+2.2f*r*cw_sin(a),z},
                   (t3_vec3){x+2.7f*r*cw_cos(b),cy+2.2f*r*cw_sin(b),z},.012f);
    }
  }
  fossil_bone(root,p,silk,(t3_vec3){x,cy+2.2f,z+.15f},(t3_vec3){x,cy-.6f,z+.15f},.018f);
  t3_object *spider=t3_group_new();spider->position=(t3_vec3){x,cy-.6f,z+.35f};t3_object_add(root,spider);
  prop(spider,p->ball,shell,0,.45f,-.15f,.65f,.8f,.4f);
  prop(spider,p->ball,shell,0,-.25f,.15f,.42f,.38f,.35f);
  for(int side=-1;side<=1;side+=2)for(int leg=0;leg<4;leg++) {
    float yy=.8f-leg*.38f,tip=1.35f-leg*.85f;
    fossil_bone(spider,p,shell,(t3_vec3){side*.35f,yy,0},(t3_vec3){side*(1.1f+leg*.1f),yy+.3f,.15f},.07f);
    fossil_bone(spider,p,shell,(t3_vec3){side*(1.1f+leg*.1f),yy+.3f,.15f},(t3_vec3){side*1.7f,tip,.05f},.045f);
  }
  for(int side=-1;side<=1;side+=2) {
    prop(spider,p->ball,eyes,side*.15f,-.23f,.46f,.1f,.1f,.055f);
    prop(spider,p->ball,eyes,side*.31f,-.12f,.35f,.055f,.055f,.04f);
    t3_mesh *fang=prop(spider,p->cone,p->cream,side*.15f,-.61f,.29f,.07f,.28f,.07f);
    t3_object_set_rotation(&fang->base,0,0,3.1415927f);
  }
  merge_still(spider,0,spider->child_count,false);
  if(whimsy_motion_count<96)whimsy_motion_list[whimsy_motion_count++]=(whimsy_motion){.object=spider,.y=cy-.6f,.phase=1};
  t3_release(spider);t3_release(sheet);t3_release(silk);t3_release(shell);t3_release(eyes);
}
