/* Hand-built theme landmarks. Shared opaque materials and low-poly geometry;
 * direct children of the scenery root so merge_still batches them by material.
 * No extra render passes or lights: emissive accents use the existing bloom. */
typedef struct whimsy_parts {
  t3_geometry *box, *ball, *cone, *cyl, *ring;
  t3_material *red, *cream, *gold, *blue, *green, *pink, *brown, *black, *glass, *lamp;
} whimsy_parts;
/* Only a handful of accents animate. Everything else stays in merged batches. */
typedef struct whimsy_motion { t3_object *object; float y, phase; bool wave; float x; int kind; float z; t3_object *foot; } whimsy_motion;
static whimsy_motion whimsy_motion_list[96];
static int whimsy_motion_count;
static float whimsy_time;
static void whimsy_bob(t3_mesh *m, float phase) {
  if (whimsy_motion_count < 96) whimsy_motion_list[whimsy_motion_count++] = (whimsy_motion){&m->base,m->base.position.y,phase,false};
}
static void whimsy_tick(void) {
  whimsy_time += 1.0f / 60;
  for (int i=0;i<whimsy_motion_count;i++) {
    whimsy_motion *a=&whimsy_motion_list[i];
    if(a->kind==14) continue; /* Legs solve after their moving parent. */
    if(a->kind==2) {
      float t=whimsy_time*2.8f+a->phase;
      a->object->position.x=a->x+.8f*cw_cos(t);
      a->object->position.y=a->y+1.7f*fabsf(cw_sin(t));
    }
    else if(a->kind==3) t3_object_set_rotation(a->object,.38f*cw_sin(whimsy_time*2+a->phase),0,0);
    else if(a->kind==4) {
      /* Start the retreat when a rider approaches; never teleport back on screen. */
      if(bike_rider_near(a->x,32))a->wave=true;
      if(a->wave)a->z+=1.f/60;
      float walk=a->z*.32f;
      a->object->position.x=a->x+walk*.6f;
      a->object->position.z=a->y-walk*.8f;
      a->object->position.y=bank_surface_y(a->object->position.x,a->object->position.z,1)+.03f*cw_cos(whimsy_time*4);
    }
    else if(a->kind==5) {
      float f=fmodf(whimsy_time*.55f+a->phase,1);
      a->object->position.y=a->x*(1-f)+a->y*f;
      a->object->position.z=a->z+5*f*f;
    }
    else if(a->kind==6) {
      float f=fmodf(whimsy_time*.4f+a->phase,1),r=.3f+f*1.8f;
      a->object->scale=(t3_vec3){r,r,.12f*(1-f)+.01f};
    }
    else if(a->kind==7) t3_object_set_rotation(a->object,0,0,.17f*cw_sin(whimsy_time*1.2f+a->phase));
    else if(a->kind==13) t3_object_set_rotation(a->object,0,0,whimsy_time*(.18f+a->phase*.1f));
    else if(a->kind==12) t3_object_set_rotation(a->object,0,whimsy_time*(a->phase?.32f:.85f),0);
    else if(a->kind==11) t3_object_set_rotation(a->object,0,0,whimsy_time*.45f);
    else if(a->kind==10) {
      if(bike_rider_near(a->x,28))a->wave=true;
      if(a->wave)a->z=fminf(a->z+1.f/60,3);
      float wake=a->z/3;
      t3_object_set_rotation(a->object,0,wake*.22f*cw_sin(whimsy_time*.6f),-.08f*(1-wake));
    }
    else if(a->kind==9) t3_object_set_rotation(a->object,-.45f,.4f*cw_sin(whimsy_time*.22f),0);
    else if(a->kind==8) t3_object_set_rotation(a->object,0,.3f*cw_sin(whimsy_time*.8f+a->phase),.07f*cw_sin(whimsy_time*1.6f));
    else if(a->wave) t3_object_set_rotation(a->object,0,0,-0.65f+0.28f*cw_sin(whimsy_time*3));
    else a->object->position.y=a->y+0.12f*cw_sin(whimsy_time*1.8f+a->phase);
  }
  /* A planted half-cycle matches the body's travel speed. Swing feet clear
   * the snow; endpoint height follows the rendered bank, not its smooth source. */
  for(int i=0;i<whimsy_motion_count;i++) {
    whimsy_motion *a=&whimsy_motion_list[i];if(a->kind!=14)continue;
    t3_object *body=a->object->parent;
    float elapsed=0;
    for(int j=0;j<whimsy_motion_count;j++)if(whimsy_motion_list[j].object==body)elapsed=whimsy_motion_list[j].z;
    float f=fmodf(elapsed/2.8f+a->phase,1),u=f<.5f?f*2:(f-.5f)*2;
    float stride=f<.5f?.28f-.56f*u:-.28f+.56f*u*u*(3-2*u);
    float lift=elapsed>0&&f>=.5f?.22f*cw_sin(u*3.1415927f):0;
    float lx=a->object->position.x,lz=stride;
    const float sn=.6f,cs=-.8f,scale=.8f;
    float wx=body->position.x+scale*(cs*lx+sn*lz),wz=body->position.z+scale*(-sn*lx+cs*lz);
    float floor=bank_surface_y(wx,wz,1);
    for(int end=-1;end<=1;end+=2) {
      floor=fmaxf(floor,bank_surface_y(wx+sn*end*.28f,wz+cs*end*.28f,1));
      floor=fmaxf(floor,bank_surface_y(wx+cs*end*.18f,wz-sn*end*.18f,1));
    }
    float fy=(floor-body->position.y)/scale+.2f+lift;
    a->foot->position=(t3_vec3){lx,fy,lz};
    float dy=1.5f-fy,dz=lz;
    a->object->scale.y=sqrtf(dy*dy+dz*dz)/1.3f;
    t3_object_set_rotation(a->object,-cw_atan2(dz,dy),0,0);
  }
}
static t3_material *toy_paint(float r, float g, float b) {
  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  m->color = lin(r, g, b);
  m->roughness = 0.38f;
  m->clearcoat = 0.45f;
  m->clearcoat_roughness = 0.25f;
  return m;
}
static t3_mesh *prop(t3_object *root, t3_geometry *geo, t3_material *m,
                     float x, float y, float z, float sx, float sy, float sz) {
  t3_mesh *o = add(root, geo, m, true);
  o->base.position = (t3_vec3){ x, y, z };
  o->base.scale = (t3_vec3){ sx, sy, sz };
  return o;
}
/* A tapered five-point star facing the rider, one small shared geometry. */
static t3_geometry *holiday_star(void) {
  mb b = {0};
  int c = mb_v(&b, (t3_vec3){0,0,0.12f}, (t3_vec3){0,0,1}, 0,0,1);
  for (int i = 0; i < 10; i++) {
    float a = 1.5707963f + i * 0.6283185f, r = i & 1 ? 0.43f : 1;
    mb_v(&b, (t3_vec3){cw_cos(a)*r,cw_sin(a)*r,0}, (t3_vec3){0,0,1}, 0,0,1);
  }
  for (int i=0;i<10;i++) mb_t(&b,c,i+1,(i+1)%10+1);
  return mb_done(&b);
}
static void festive_tree(t3_object *root, const whimsy_parts *p, t3_geometry *star, float x, float y, float z, float s) {
  prop(root,p->cyl,p->brown,x,y+0.5f*s,z,0.16f*s,s,0.16f*s);
  for (int j=0;j<3;j++) {
    float r=(1.4f-j*0.36f)*s;
    prop(root,p->cone,p->green,x,y+(1.4f+j*0.85f)*s,z,r,1.9f*s,r);
    for (int k=0;k<6;k++) {
      float a=k*1.0471976f+j*0.5f;
      prop(root,p->ball,k%2?p->red:p->gold,x+cw_cos(a)*r*0.78f,y+(0.9f+j*0.85f)*s,z+cw_sin(a)*r*0.78f,0.12f*s,0.12f*s,0.12f*s);
    }
  }
  prop(root,star,p->lamp,x,y+4.1f*s,z,0.42f*s,0.42f*s,0.42f*s);
}
static void santa_sleigh(t3_object *root, const whimsy_parts *p, float x, float y, float z) {
  /* Sleigh, gold runners, a waving Santa, beard, hat and a sack of gifts. */
  prop(root,p->box,p->red,x,y+0.8f,z,3.2f,0.8f,1.5f);
  prop(root,p->box,p->red,x-1.3f,y+1.4f,z,0.3f,1.2f,1.5f);
  for(int side=-1;side<=1;side+=2) {
    prop(root,p->box,p->gold,x,y+0.15f,z+side*0.8f,3.8f,0.14f,0.12f);
    t3_mesh *curl=prop(root,p->ring,p->gold,x+1.8f,y+0.5f,z+side*0.8f,0.45f,0.45f,0.45f);
    (void)curl;
  }
  prop(root,p->ball,p->red,x,y+1.7f,z,0.65f,0.8f,0.55f);
  prop(root,p->box,p->black,x,y+1.5f,z+0.5f,1.1f,0.16f,0.08f);
  prop(root,p->box,p->gold,x,y+1.5f,z+0.56f,0.24f,0.24f,0.08f);
  prop(root,p->ball,p->cream,x,y+2.55f,z,0.43f,0.45f,0.4f);
  prop(root,p->ball,p->cream,x,y+2.25f,z+0.28f,0.4f,0.48f,0.2f);
  for(int s=-1;s<=1;s+=2) prop(root,p->ball,p->black,x+s*0.15f,y+2.66f,z+0.37f,0.045f,0.055f,0.045f);
  prop(root,p->cone,p->red,x+0.1f,y+3.12f,z,0.46f,0.8f,0.46f);
  prop(root,p->ball,p->cream,x+0.1f,y+3.55f,z,0.16f,0.16f,0.16f);
  t3_object *wave=t3_group_new();
  wave->position=(t3_vec3){x+0.4f,y+1.8f,z};
  t3_object_set_rotation(wave,0,0,-0.65f);
  t3_object_add(root,wave);
  prop(wave,p->cyl,p->red,0,0.5f,0,0.16f,1.1f,0.16f);
  prop(wave,p->ball,p->cream,0,1.1f,0,0.2f,0.2f,0.2f);
  if(whimsy_motion_count<96) whimsy_motion_list[whimsy_motion_count++]=(whimsy_motion){wave,0,0,true};
  t3_release(wave);
  prop(root,p->ball,p->green,x-0.9f,y+1.65f,z-0.1f,0.6f,0.8f,0.6f);
  /* Four reindeer in two pairs, each with branched antlers and little hooves. */
  for(int i=0;i<4;i++) {
    float dx=x+4+(i/2)*3.2f, dz=z+(i%2?0.7f:-0.7f);
    /* The caller scales the scene around the sleigh by .75. Sample each
     * animal's final world position, then undo that scale for authored height. */
    float ry=y+(bank_surface_y(x+(dx-x)*.75f,z+(dz-z)*.75f,1)-y)/.75f;
    prop(root,p->ball,p->brown,dx,ry+1.15f,dz,0.95f,0.48f,0.35f);
    prop(root,p->ball,p->brown,dx+0.7f,ry+1.85f,dz,0.3f,0.65f,0.28f);
    prop(root,p->ball,p->brown,dx+0.95f,ry+2.1f,dz,0.45f,0.26f,0.24f);
    prop(root,p->ball,i>=2?p->red:p->black,dx+1.36f,ry+2.1f,dz,0.12f,0.12f,0.12f);
    prop(root,p->ball,p->black,dx+0.94f,ry+2.22f,dz+0.22f,0.045f,0.045f,0.045f);
    for(int j=0;j<4;j++) {
      float lx=dx+(j/2?0.6f:-0.6f), lz=dz+(j%2?0.2f:-0.2f);
      prop(root,p->cyl,p->brown,lx,ry+0.6f,lz,0.07f,1,0.07f);
      prop(root,p->box,p->black,lx+0.06f,ry+0.12f,lz,0.25f,0.2f,0.16f);
    }
    for(int a=-1;a<=1;a+=2) {
      prop(root,p->cyl,p->gold,dx+0.65f,ry+2.7f,dz+a*0.2f,0.045f,0.8f,0.045f);
      for(int j=0;j<2;j++) {
        t3_mesh *ant=prop(root,p->cyl,p->gold,dx+0.53f,ry+2.6f+j*0.25f,dz+a*0.26f,0.035f,0.35f,0.035f);
        t3_object_set_rotation(&ant->base,0,0,0.85f);
      }
    }
  }
  for(int s=-1;s<=1;s+=2) prop(root,p->box,p->gold,x+4.8f,y+0.95f,z+s*0.8f,8,0.035f,0.035f);
}
/* Canvas inspired by real two-king-pole oval big tops, with low side walls,
 * concave tensioned roof panels and separate scalloped valances. */
static void circus_quad(mb *b,t3_vec3 a,t3_vec3 c,t3_vec3 d,t3_vec3 e) {
  t3_vec3 u={c.x-a.x,c.y-a.y,c.z-a.z},v={d.x-a.x,d.y-a.y,d.z-a.z};
  t3_vec3 n={u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
  if(n.x*n.x+n.y*n.y+n.z*n.z<.0000001f)return;
  int k=mb_v(b,a,n,0,0,1);mb_v(b,c,n,1,0,1);mb_v(b,d,n,1,1,1);mb_v(b,e,n,0,1,1);
  mb_t(b,k,k+1,k+2);mb_t(b,k,k+2,k+3);
}
static t3_vec3 circus_roof(float a,float t,float radius,float span,float wall,float rise) {
  float c=cw_cos(a),s=cw_sin(a);
  float crest=wall+rise-.65f*(span>0?s*s:0);
  return (t3_vec3){c*(span+radius*t),wall+(crest-wall)*(1-t)*(1-t),s*(.035f+radius*t)};
}
static void circus_stay(t3_object *root,const whimsy_parts *p,t3_material *mat,t3_vec3 a,t3_vec3 b,float thick) {
  float dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z;
  float len=sqrtf(dx*dx+dy*dy+dz*dz);
  t3_mesh *m=prop(root,p->cyl,mat,(a.x+b.x)*.5f,(a.y+b.y)*.5f,(a.z+b.z)*.5f,thick,len,thick);
  t3_object_set_rotation(&m->base,0,cw_atan2(-dz,dx),-cw_atan2(sqrtf(dx*dx+dz*dz),dy));
}
static void circus_tent(t3_object *root,const whimsy_parts *p,float x,float y,float z,
                        float radius,float span,float wall,float rise,t3_material *a,t3_material *b,t3_material *trim) {
  mb panels[2]={{0}},frill={0};
  enum { SECTORS=64, RINGS=10 };
  /* Raise the eaves only where a hillside requires it. The skirt follows
   * the whole footprint and overlaps terrain, rather than ending at center Y. */
  for(int j=0;j<SECTORS;j++) {
    float angle=j*6.2831853f/SECTORS;
    float ground=bank_surface_y(x+cw_cos(angle)*(radius+span),z+cw_sin(angle)*(radius+.035f),0);
    wall=fmaxf(wall,ground-y+1.5f);
  }
  for(int j=0;j<SECTORS;j++) {
    float a0=j*6.2831853f/SECTORS,a1=(j+1)*6.2831853f/SECTORS;
    mb *mesh=&panels[(j/2)&1];
    for(int k=0;k<RINGS;k++) {
      float t0=(float)k/RINGS,t1=(float)(k+1)/RINGS;
      circus_quad(mesh,circus_roof(a0,t0,radius,span,wall,rise),circus_roof(a1,t0,radius,span,wall,rise),
                  circus_roof(a1,t1,radius,span,wall,rise),circus_roof(a0,t1,radius,span,wall,rise));
    }
    t3_vec3 q0=circus_roof(a0,1,radius,span,wall,rise),q1=circus_roof(a1,1,radius,span,wall,rise);
    t3_vec3 bot0=q0,bot1=q1;
    float low=1e9f;
    for(int sample=0;sample<=4;sample++) {
      float f=sample*.25f;
      low=fminf(low,bank_surface_y(x+q0.x+(q1.x-q0.x)*f,z+q0.z+(q1.z-q0.z)*f,0));
    }
    bot0.y=bot1.y=low-y-.3f;
    /* Leave a real open doorway toward the rider, rather than a black decal. */
    if(!(cw_sin((a0+a1)*.5f)>.9f && fabsf((q0.x+q1.x)*.5f)<1.8f))
      circus_quad(mesh,bot0,q0,q1,bot1);
    bot0=q0;bot1=q1;
    bot0.y-=.28f+.22f*(j%2);bot1.y-=.28f+.22f*((j+1)%2);
    q0.x*=1.008f;q0.z*=1.008f;q1.x*=1.008f;q1.z*=1.008f;
    bot0.x*=1.008f;bot0.z*=1.008f;bot1.x*=1.008f;bot1.z*=1.008f;
    circus_quad(&frill,bot0,q0,q1,bot1);
  }
  t3_material *mats[]={a,b,trim};
  for(int k=0;k<3;k++) {
    t3_geometry *g=mb_done(k<2?&panels[k]:&frill);
    prop(root,g,mats[k],x,y,z,1,1,1);t3_release(g);
  }
  int poles=span>.1f?2:1;
  for(int i=0;i<poles;i++) {
    float px=x+(poles==1?0:(i?span:-span));
    float bottom=bank_surface_y(px,z,0)-.25f,top=y+wall+rise+.8f;
    prop(root,p->cyl,p->brown,px,(bottom+top)*.5f,z,.09f,top-bottom,.09f);
    mb flag={0};
    circus_quad(&flag,(t3_vec3){0,0,0},(t3_vec3){1.7f,-.35f,.1f},(t3_vec3){.35f,-.65f,0},(t3_vec3){0,-.65f,0});
    t3_geometry *g=mb_done(&flag);prop(root,g,trim,px,y+wall+rise+.8f,z,1,1,1);t3_release(g);
  }
  for(int i=0;i<16;i++) {
    float ang=i*6.2831853f/16,c=cw_cos(ang),s=cw_sin(ang);
    if(s>.9f)continue;
    t3_vec3 edge={x+c*(radius+span),y+wall,z+s*radius};
    t3_vec3 peg={x+c*(radius+span+2.4f),y+.12f,z+s*(radius+2.4f)};
    peg.y=bank_surface_y(peg.x,peg.z,0)+.12f;
    circus_stay(root,p,p->cream,edge,peg,.022f);
    float bottom=bank_surface_y(edge.x,edge.z,0)-.25f;
    prop(root,p->cyl,p->brown,edge.x,(bottom+edge.y)*.5f,edge.z,.045f,edge.y-bottom,.045f);
    prop(root,p->cyl,p->gold,peg.x,peg.y+.06f,peg.z,.055f,.4f,.055f);
  }
  /* Open entrance canopy, tied-back curtains, and a row of warm bulbs. */
  float front=z+radius;
  for(int side=-1;side<=1;side+=2) {
    float bottom=bank_surface_y(x+side*1.9f,front+2,0)-.25f,top=y+wall-.2f;
    prop(root,p->cyl,p->gold,x+side*1.9f,(bottom+top)*.5f,front+2,.065f,top-bottom,.065f);
    t3_mesh *curtain=prop(root,p->cone,a,x+side*1.55f,y+wall*.5f,front+.05f,.5f,wall,.18f);
    t3_object_set_rotation(&curtain->base,0,0,side*.18f);
  }
  mb awning={0};
  circus_quad(&awning,(t3_vec3){-2,wall,0},(t3_vec3){2,wall,0},(t3_vec3){2,wall-.2f,2.2f},(t3_vec3){-2,wall-.2f,2.2f});
  t3_geometry *g=mb_done(&awning);prop(root,g,trim,x,y,front,1,1,1);t3_release(g);
  for(int i=0;i<9;i++)prop(root,p->ball,p->lamp,x-1.8f+i*.45f,y+wall-.25f,front+2.2f,.065f,.065f,.065f);
}
static void circus_clown(t3_object *root,const whimsy_parts *p,float x,float y,float z,int style) {
  t3_material *suit=style%2?p->blue:p->pink,*hair=style%2?p->red:p->gold;
  for(int side=-1;side<=1;side+=2) {
    prop(root,p->ball,p->red,x+side*.3f,y+.16f,z+.22f,.32f,.18f,.6f);
    prop(root,p->cyl,suit,x+side*.26f,y+.65f,z,.23f,1,.23f);
  }
  prop(root,p->ball,suit,x,y+1.35f,z,.58f,.7f,.38f);
  for(int j=0;j<3;j++)prop(root,p->ball,p->gold,x,y+1.1f+j*.24f,z+.37f,.09f,.09f,.07f);
  for(int j=0;j<10;j++) {
    float a=j*.6283185f;
    prop(root,p->ball,p->cream,x+cw_cos(a)*.37f,y+1.93f,z+cw_sin(a)*.28f,.18f,.1f,.16f);
  }
  prop(root,p->ball,p->cream,x,y+2.28f,z,.37f,.4f,.33f);
  for(int side=-1;side<=1;side+=2) {
    prop(root,p->ball,hair,x+side*.37f,y+2.32f,z-.06f,.25f,.3f,.24f);
    prop(root,p->ball,p->black,x+side*.13f,y+2.39f,z+.3f,.045f,.075f,.035f);
    circus_stay(root,p,suit,(t3_vec3){x+side*.42f,y+1.7f,z},(t3_vec3){x+side*.85f,y+1.95f,z+.15f},.14f);
    prop(root,p->ball,p->cream,x+side*.88f,y+2,z+.15f,.17f,.17f,.16f);
  }
  prop(root,p->ball,p->red,x,y+2.28f,z+.37f,.13f,.13f,.13f);
  prop(root,p->ball,p->red,x,y+2.08f,z+.29f,.16f,.065f,.055f);
  prop(root,p->cone,style%2?p->gold:p->blue,x+.05f,y+2.93f,z,.32f,.8f,.32f);
  prop(root,p->ball,p->cream,x+.05f,y+3.34f,z,.1f,.1f,.1f);
  /* The balls move in a three-ball cascade; static body stays batched. */
  for(int j=0;j<3;j++) {
    t3_mesh *ball=prop(root,p->ball,j==0?p->gold:j==1?p->red:p->blue,x,y+3.1f,z+.35f,.14f,.14f,.14f);
    if(whimsy_motion_count<96)whimsy_motion_list[whimsy_motion_count++]=(whimsy_motion){&ball->base,y+2.05f,j*2.0943951f,false,x,2};
  }
}

#include "gummy-mesh.inc"
static t3_geometry *gummy_geometry(void) {
  mb b={0};
  for(unsigned i=0;i<sizeof gummy_vertices/sizeof gummy_vertices[0];i++) {
    const float *v=gummy_vertices[i];
    mb_v(&b,(t3_vec3){v[0],v[1],v[2]},(t3_vec3){v[3],v[4],v[5]},0,0,1);
  }
  for(unsigned i=0;i<sizeof gummy_triangles/sizeof gummy_triangles[0];i++)
    mb_t(&b,gummy_triangles[i][0],gummy_triangles[i][1],gummy_triangles[i][2]);
  return mb_done(&b);
}
static float chocolate_ground(float x,float z) {
  return z < -TRACK_HALF_W ? bank_y(x,z,0):track_height_at(x,NULL);
}
static float chocolate_surface(float x,float z) {
  if(z>=-TRACK_HALF_W)return track_height_at(x,NULL);
  if(z<=-TRACK_HALF_W-.3f)return bank_surface_y(x,z,0);
  float t=(-TRACK_HALF_W-z)/.3f;
  return track_height_at(x,NULL)*(1-t)+bank_surface_y(x,-TRACK_HALF_W-.3f,0)*t;
}
static void chocolate_quad(mb *b,t3_vec3 *q,bool pool,bool fall,float fall_height,float f0,float f1) {
  int ids[4];
  for(int v=0;v<4;v++) {
    t3_vec3 n;
    if(fall) { float f=v>=2?f1:f0;n=(t3_vec3){0,10*f,fall_height}; }
    else {
      float dx=(chocolate_ground(q[v].x+.15f,q[v].z)-chocolate_ground(q[v].x-.15f,q[v].z))/.3f;
      float dz=(chocolate_ground(q[v].x,q[v].z+.15f)-chocolate_ground(q[v].x,q[v].z-.15f))/.3f;
      n=(t3_vec3){-dx,1,-dz};
    }
    ids[v]=mb_v(b,q[v],n,0,0,1);
  }
  if(pool){mb_t(b,ids[0],ids[1],ids[2]);mb_t(b,ids[0],ids[2],ids[3]);}
  else{mb_t(b,ids[3],ids[2],ids[1]);mb_t(b,ids[3],ids[1],ids[0]);}
}
static void chocolate_falls(t3_object *root,const whimsy_parts *p,const track_desc *t) {
  float x=t->finish_x*.73f,z=-25,y=bank_y(x,z,0);
  t3_material *choc=toy_paint(.25f,.095f,.035f),*milk=toy_paint(.53f,.28f,.12f);
  choc->roughness=.29f;choc->clearcoat=.4f;
  milk->roughness=.3f;
  /* A broken chocolate-bar escarpment, with wafer layers beneath the fall. */
  for(int j=0;j<5;j++)for(int k=0;k<3;k++) {
    float h=8+(j%3)*1.3f,px=x+(j-2)*2.7f;
    prop(root,p->box,k==1?p->cream:p->brown,px,y+(k+.5f)*h/3,z-2,2.58f,h/3-.12f,5.5f);
    prop(root,p->box,milk,px,y+h+.15f,z-2,2.3f,.5f,5.15f);
  }
  prop(root,p->ball,choc,x,y+10.1f,z-1.9f,4.1f,.35f,3.2f);
  /* Terrain-conforming chocolate: no unsupported ellipsoid above the bank. */
  mb pool={0};
  for(int j=0;j<48;j++)for(int k=0;k<8;k++) {
    t3_vec3 q[4];
    for(int v=0;v<4;v++) {
      float a=(j+(v==1||v==2))*6.2831853f/48,r=(k+(v>=2))/8.f;
      float px=x+cw_cos(a)*8*r,pz=z+5+cw_sin(a)*5.3f*r;
      q[v]=(t3_vec3){px,chocolate_surface(px,pz)+.08f,pz};
    }
    chocolate_quad(&pool,q,true,false,0,0,0);
  }
  t3_geometry *pg=mb_done(&pool);add(root,pg,choc,false);t3_release(pg);
  /* Two shallow streams, one from this fall, continuing across the riding deck. */
  for(int crossing=0;crossing<2;crossing++) {
    float cx=t->finish_x*(crossing?.73f:.34f);mb river={0};
    for(int j=0;j<60;j++)for(int k=0;k<8;k++) {
      t3_vec3 q[4];
      for(int v=0;v<4;v++) {
        float pz=-23+(j+(v>=2))*.42f;
        float px=cx+(k+(v==1||v==2)-4)*(.75f+.055f*cw_sin(pz*2.1f))+.16f*cw_sin(pz*1.7f);
        float ground=chocolate_surface(px,pz);
        q[v]=(t3_vec3){px,ground+.08f,pz};
      }
      chocolate_quad(&river,q,false,false,0,0,0);
    }
    t3_geometry *rg=mb_done(&river);add(root,rg,choc,false);t3_release(rg);
  }
  /* The opaque curtain curves outward into the plunge pool. */
  mb curtain={0};
  for(int row=0;row<24;row++)for(int col=0;col<12;col++) {
    t3_vec3 q[4];
    for(int v=0;v<4;v++) {
      float u=(col+(v==1||v==2))/12.f;
      float f=(row+(v>=2))/24.f;
      q[v]=(t3_vec3){x+(u-.5f)*(6+.7f*cw_sin(f*5)),(y+10.4f)*(1-f)+(bank_y(x+(u-.5f)*6,z+5.9f,0)+.07f)*f,z+.9f+5*f*f+.10f*cw_sin(u*37)};
    }
    chocolate_quad(&curtain,q,false,true,y+10.4f-bank_y(x,z+5.9f,0),row/24.f,(row+1)/24.f);
  }
  t3_geometry *g=mb_done(&curtain);add(root,g,choc,true);t3_release(g);
  for(int i=0;i<16;i++) {
    float px=x-2.7f+(i%8)*.77f;
    t3_mesh *drop=prop(root,p->ball,i%4?choc:milk,px,y+5,z+2,.12f,.65f,.14f);
    if(whimsy_motion_count<96)whimsy_motion_list[whimsy_motion_count++]=(whimsy_motion){.object=&drop->base,.y=chocolate_surface(px,z+5.96f)+.15f,.x=y+10.4f,.phase=i*.173f,.kind=5,.z=z+.96f};
  }
  /* Chocolate chunks and wafer stepping stones frame the banks. */
  for(int i=0;i<14;i++) {
    float a=i*6.2831853f/14,px=x+cw_cos(a)*8.4f,pz=z+5+cw_sin(a)*5.7f;
    t3_mesh *chunk=prop(root,p->box,i%3?milk:p->cream,px,bank_y(px,pz,0)+.25f,pz,.9f,.5f,.65f);
    t3_object_set_rotation(&chunk->base,.1f,a,.1f);
  }
  t3_release(choc);t3_release(milk);
}

static void candy_gumdrops(t3_object *root,const whimsy_parts *p,const track_desc *t) {
  mb mesh={0};
  const float profile[7][2]={{1,0},{1.02f,.18f},{.95f,.6f},{.79f,1.1f},{.54f,1.48f},{.26f,1.68f},{.001f,1.74f}};
  for(int j=0;j<32;j++)for(int k=0;k<6;k++) {
    float a=j*6.2831853f/32,b=(j+1)*6.2831853f/32;
    /* Smooth normals follow the rounded profile; positions/triangle count
     * stay the same instead of using a separate normal on every flat quad. */
    int first=mesh.nv;
    for(int corner=0;corner<4;corner++) {
      int ring=k+(corner==1||corner==2),lo=ring?ring-1:0,hi=ring<6?ring+1:6;
      float angle=corner<2?a:b,dr=profile[hi][0]-profile[lo][0],dy=profile[hi][1]-profile[lo][1];
      float length=sqrtf(dr*dr+dy*dy);
      t3_vec3 normal={cw_cos(angle)*dy/length,-dr/length,cw_sin(angle)*dy/length};
      if(ring==6)normal=(t3_vec3){0,1,0};
      mb_v(&mesh,(t3_vec3){cw_cos(angle)*profile[ring][0],profile[ring][1],cw_sin(angle)*profile[ring][0]},normal,corner>=2,ring/6.f,1);
    }
    mb_t(&mesh,first,first+1,first+2);mb_t(&mesh,first,first+2,first+3);
  }
  t3_geometry *g=mb_done(&mesh);
  const unsigned hues[]={0xff3268,0x37dba5,0xffa32b,0x8064ee};
  for(int color=0;color<4;color++) {
    t3_material *jelly=t3_mesh_physical_material_new(0xffffff);
    jelly->color=(t3_color){.95f,.95f,.95f};jelly->roughness=.19f;
    jelly->transmission=.78f;jelly->thickness=1.5f;jelly->ior=1.38f;
    jelly->attenuation_color=t3_color_hex(hues[color]);jelly->attenuation_distance=1.5f;
    for(int n=0;n<3;n++) {
      float x=t->finish_x*(.35f+.21f*n)+color*3.3f,z=-9-color*2.3f-(n%2)*6;
      float y=bank_surface_y(x,z,0),scale=.55f+((color+n*3)%5)*.4f;
      for(int edge=0;edge<16;edge++) {
        float angle=edge*6.2831853f/16;
        y=fminf(y,bank_surface_y(x+cw_cos(angle)*1.02f*scale,z+cw_sin(angle)*1.02f*scale,0));
      }
      y-=.08f;
      prop(root,g,jelly,x,y,z,scale,scale,scale);
      /* Sugar crystals on the outside remain opaque, sharing one material. */
      for(int k=0;k<7;k++) {
        float a=k*2.3999632f;
        prop(root,p->box,p->cream,x+cw_cos(a)*.75f*scale,y+(.35f+(k%3)*.3f)*scale,z+cw_sin(a)*.75f*scale,.025f*scale,.035f*scale,.025f*scale);
      }
    }
    t3_release(jelly);
  }
  t3_release(g);
}
/* Clip paving to the bank's existing triangles: exact seating even where
 * a tile crosses a terrain-cell diagonal. Generated once, then batched. */
static void fantasy_tile(mb *mesh,float px,float pz) {
  float bounds[4]={px-.75f,px+.75f,pz-.525f,pz+.525f};
  float edge=-TRACK_HALF_W-.3f;
  for(int ix=(int)floorf(bounds[0]);ix<=(int)floorf(bounds[1]);ix++)
    for(int row=0;row<(int)CW_ARRAY_LEN(BANK_ZS)-1;row++) {
      float z0=edge-BANK_ZS[row],z1=edge-BANK_ZS[row+1];
      if(z0<bounds[2]||z1>bounds[3])continue;
      t3_vec3 v[4]={{ix,0,z0},{ix+1,0,z0},{ix+1,0,z1},{ix,0,z1}};
      for(int j=0;j<4;j++)v[j].y=bank_surface_y(v[j].x,v[j].z,0)+.045f;
      const int tris[2][3]={{0,1,2},{0,2,3}};
      for(int tri=0;tri<2;tri++) {
        t3_vec3 poly[12],out[12];int n=3;
        for(int j=0;j<3;j++)poly[j]=v[tris[tri][j]];
        for(int plane=0;plane<4&&n;plane++) {
          int count=0;
          for(int j=0;j<n;j++) {
            t3_vec3 a=poly[j],b=poly[(j+1)%n];
            float da=(plane<2?a.x:a.z)-bounds[plane],db=(plane<2?b.x:b.z)-bounds[plane];
            if(plane%2){da=-da;db=-db;}
            if(da>=0)out[count++]=a;
            if((da>=0)!=(db>=0)) {
              float t=da/(da-db);
              out[count++]=(t3_vec3){a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
            }
          }
          n=count;memcpy(poly,out,n*sizeof *poly);
        }
        for(int j=1;j+1<n;j++) {
          t3_vec3 a=poly[0],b=poly[j],c=poly[j+1];
          t3_vec3 u={b.x-a.x,b.y-a.y,b.z-a.z},v={c.x-a.x,c.y-a.y,c.z-a.z};
          t3_vec3 normal={u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
          if(normal.x*normal.x+normal.y*normal.y+normal.z*normal.z<1e-10f)continue;
          int first=mb_v(mesh,a,normal,0,0,1);mb_v(mesh,b,normal,1,0,1);mb_v(mesh,c,normal,1,1,1);
          mb_t(mesh,first,first+1,first+2);
        }
        /* Only tile perimeter edges get skirts; internal triangulation stays hidden. */
        for(int j=0;j<n;j++) {
          t3_vec3 a=poly[j],b=poly[(j+1)%n];bool border=false;
          for(int plane=0;plane<4;plane++)
            if(fabsf((plane<2?a.x:a.z)-bounds[plane])<.0001f&&fabsf((plane<2?b.x:b.z)-bounds[plane])<.0001f)border=true;
          if(border){t3_vec3 c=b,d=a;c.y-=.16f;d.y-=.16f;circus_quad(mesh,a,d,c,b);}
        }
      }
    }
}
/* Small settlements extend away from the race route. Footpaths follow the
 * actual terrain; static house pieces remain eligible for spatial batching. */
static void fantasy_settlement(t3_object *root,const whimsy_parts *p,const track_desc *t,bool candy) {
  t3_material *palette[]={p->pink,p->blue,p->gold,p->green,p->red,p->cream};
  for(int village=0;village<6;village++) {
    float center=38+village*(t->finish_x-70)/6;
    for(int branch=0;branch<3;branch++) {
      float hx=center+(branch-1)*16, hz=-18-branch*13;
      float scale=.7f+((village*3+branch*5)%7)*.21f;
      if(candy)for(int river=0;river<2;river++) {
        float cx=t->finish_x*(river?.73f:.34f),clear=3.8f+3.5f*scale;
        if(fabsf(hx-cx)<clear)hx=cx+(hx<cx?-clear:clear);
      }
      float hy=bank_y(hx,hz,0),height=(candy?4.5f:5.5f)*scale;
      t3_material *color=palette[(village+branch*2)%6];
      /* Keep the existing floor elevation, but bury the foundation below
       * the whole footprint rather than only the centre of a hillside. */
      float foundation_low=hy-1.2f,foundation_top=hy+.6f;
      int nx=(int)ceilf(7*scale/.4f),nz=(int)ceilf(6*scale/.4f);
      for(int side=0;side<4;side++) {
        int count=side<2?nx:nz;
        for(int j=0;j<=count;j++) {
          float u=(float)j/count;
          float xx=hx+(side<2?(u-.5f)*7*scale:(side==2?-3.5f:3.5f)*scale);
          float zz=hz+(side<2?(side==0?-3:3)*scale:(u-.5f)*6*scale);
          foundation_low=fminf(foundation_low,bank_surface_y(xx,zz,0)-.25f);
        }
      }
      prop(root,p->box,candy?p->brown:p->cream,hx,(foundation_low+foundation_top)*.5f,hz,
           7*scale,foundation_top-foundation_low,6*scale);
      if(candy) {
        prop(root,p->cyl,p->cream,hx,hy+height*.5f,hz,3*scale,height,3*scale);
        for(int tier=0;tier<3;tier++) {
          float cy=hy+(tier+.3f)*height/3;
          prop(root,p->cyl,color,hx,cy,hz,3.08f*scale,.23f*scale,3.08f*scale);
        }
        prop(root,p->ball,color,hx,hy+height,hz,3.35f*scale,1.1f*scale,3.35f*scale);
        for(int k=0;k<12;k++) {
          float a=k*.5235988f;
          prop(root,p->ball,p->cream,hx+cw_cos(a)*2.9f*scale,hy+height+.2f*scale,hz+cw_sin(a)*2.9f*scale,.3f*scale,.4f*scale,.3f*scale);
        }
        prop(root,p->ball,p->red,hx,hy+height+1.25f*scale,hz,.55f*scale,.55f*scale,.55f*scale);
      } else {
        prop(root,p->box,color,hx,hy+height*.5f,hz,6*scale,height,5.5f*scale);
        prop(root,p->box,p->cream,hx,hy+height,hz,6.7f*scale,.3f*scale,6.1f*scale);
        for(int k=0;k<4;k++) {
          float xx=hx+(k/2?1.7f:-1.7f)*scale,zz=hz+(k%2?1.7f:-1.7f)*scale;
          prop(root,p->cyl,color,xx,hy+height+.5f*scale,zz,.6f*scale,.7f*scale,.6f*scale);
        }
        if(branch==2) {
          prop(root,p->box,palette[(village+3)%6],hx+scale,hy+height+2*scale,hz,3.5f*scale,3.5f*scale,3.5f*scale);
          prop(root,p->cone,p->gold,hx+scale,hy+height+5*scale,hz,2.7f*scale,3*scale,2.7f*scale);
        }
      }
      float face=hz+(candy?3:2.8f)*scale;
      prop(root,p->box,p->brown,hx,hy+1.1f*scale,face,1.35f*scale,2.2f*scale,.12f);
      for(int side=-1;side<=1;side+=2) {
        prop(root,p->box,p->cream,hx+side*1.8f*scale,hy+height*.62f,face,1.2f*scale,1.6f*scale,.18f);
        prop(root,p->box,p->glass,hx+side*1.8f*scale,hy+height*.62f,face+.12f,.85f*scale,1.2f*scale,.08f);
      }
      /* Each tile's top follows the bank triangles. A horizontal box
       * spanning the highest corner made steep banks look like broken walls. */
      mb paving={0};
      int steps=(int)((-face-4)/1.5f);
      for(int k=0;k<steps;k++) {
        float u=(k+.5f)/steps;
        float px=center+(hx-center)*u+2*cw_sin(u*3.1415927f),pz=-4+(face+4)*u;
        if(candy&&chocolate_amount(t,px)>.01f)continue;
        fantasy_tile(&paving,px,pz);
      }
      t3_geometry *path=mb_done(&paving);
      prop(root,path,candy?p->gold:p->cream,0,0,0,1,1,1);t3_release(path);
    }
  }
}

#include "discoveries.inc"
#include "mesa.inc"
#include "city.inc"
#include "fantasy.inc"

static void whimsical_props(t3_object *root, const track_desc *t, const theme *th) {
  memset(forest_deer,0,sizeof forest_deer);
  memset(forest_legs,0,sizeof forest_legs);
  const char *n=th->name;
  bool circus=!strcmp(n,"circus"), candy=!strcmp(n,"candy"), toy=!strcmp(n,"toybox");
  bool xmas=!strcmp(n,"xmas"), scary=!strcmp(n,"scary"), city=!strcmp(n,"neon");
  bool beach=!strcmp(n,"beach"),cosmic=!strcmp(n,"cosmic"),coast=!strcmp(n,"coast"),canyon=!strcmp(n,"canyon"),desert=!strcmp(n,"desert"),autumn=!strcmp(n,"autumn");
  bool alpine=!strcmp(n,"alpine"),jungle=!strcmp(n,"jungle"),moon=!strcmp(n,"moon");
  if (!circus&&!candy&&!toy&&!xmas&&!scary&&!city&&!alpine&&!jungle&&!moon&&!desert&&!autumn&&!canyon&&!coast&&!cosmic&&!beach) return;
  whimsy_parts p={
    t3_box_geometry_new(1,1,1,1,1,1), t3_sphere_geometry_new(1,12,8),
    t3_cone_geometry_new(1,1,12,1,false), t3_cylinder_geometry_new(1,1,1,10,1,false),
    t3_torus_geometry_new(1,0.12f,6,24,6.2831853f),
    toy_paint(0.92f,0.12f,0.2f),toy_paint(1,0.96f,0.85f),toy_paint(1,0.72f,0.12f),
    toy_paint(0.12f,0.55f,0.95f),toy_paint(0.12f,0.55f,0.25f),toy_paint(1,0.36f,0.65f),
    toy_paint(0.42f,0.23f,0.12f),toy_paint(0.06f,0.08f,0.12f),toy_paint(0.2f,0.65f,0.8f),glow(1,0.75f,0.25f,2)
  };
  t3_material *colors[]={p.red,p.blue,p.gold,p.green,p.pink};
  int kind=th->far_kind==1?1:city?2:0;
  if(desert)desert_wreck(root,&p,t);
  if(canyon)canyon_discoveries(root,&p,t);
  if(coast)coast_discoveries(root,&p,t);
  if(cosmic)cosmic_discoveries(root,&p,t);
  if(beach)beach_discoveries(root,&p,t);
  if(scary)scary_discoveries(root,&p,t);
  if(autumn)autumn_discoveries(root,&p,t);
  if(alpine) {
    snow_walker(root,&p,t->finish_x*.52f,-6.2f,true);
    snow_walker(root,&p,t->finish_x*.85f,-6.2f,false);
  }
  if(jungle) {
    stone_circle(root,&p,t->finish_x*.58f,-8.5f);
    temple_guardian(root,&p,t->finish_x*.82f,-8);
    fantasy_camp(root,&p);
    forest_deer_build(root,&p);
  }
  if(moon) {
    lunar_wreck(root,&p,t->finish_x*.62f,-9);
    lunar_listening_post(root,&p,t->finish_x*.84f,-9);
    desert_alien(root,&p,t->finish_x*.84f+5,-6.5f,true);
  }
  if(xmas) {
    t3_geometry *star=holiday_star();
    for(int i=0;i<22;i++) {
      float x=5+i*16, z=-5.5f-(i%3)*3, s=0.65f+(i%4)*0.18f;
      if(fabsf(x-t->finish_x*.5f)<10) continue; /* room for the mid-course sleigh */
      festive_tree(root,&p,star,x,bank_y(x,z,kind),z,s);
    }
    /* Santa greets riders in the middle of Christmas Town. */
    float x=t->finish_x*.5f,z=-4.5f,y=bank_y(x,z,kind)+0.15f;
    int first=root->child_count;
    santa_sleigh(root,&p,x,y,z);
    for(int i=first;i<root->child_count;i++) {
      t3_object *o=root->children[i];
      o->position=(t3_vec3){x+(o->position.x-x)*0.75f,y+(o->position.y-y)*0.75f,z+(o->position.z-z)*0.75f};
      o->scale.x*=0.75f;o->scale.y*=0.75f;o->scale.z*=0.75f;
    }
    t3_release(star);
  }
  if(scary) for(int i=0;i<12;i++) {
    float x=12+i*36, z=-4.8f-(i%2)*2, y=bank_y(x,z,kind);
    if(fabsf(x-t->finish_x*.48f)<9||fabsf(x-t->finish_x*.85f)<8)continue;
    prop(root,p.cyl,p.brown,x,y+1.5f,z,0.09f,3,0.09f);
    t3_mesh *arms=prop(root,p.box,p.brown,x,y+2.1f,z,2.6f,0.12f,0.12f);
    t3_object_set_rotation(&arms->base,0,0,0.13f);
    prop(root,p.cone,i%2?p.pink:p.blue,x,y+1.7f,z,0.65f,1.1f,0.3f);
    prop(root,p.ball,p.gold,x,y+2.65f,z,0.43f,0.38f,0.36f);
    prop(root,p.cone,p.black,x,y+3.18f,z,0.65f,0.8f,0.6f);
    prop(root,p.cyl,p.black,x,y+2.88f,z,0.7f,0.08f,0.6f);
    for(int s=-1;s<=1;s+=2) {
      prop(root,p.ball,p.black,x+s*0.15f,y+2.69f,z+0.33f,0.07f,0.09f,0.05f);
      for(int j=0;j<3;j++) {
        t3_mesh *straw=prop(root,p.box,p.gold,x+s*(1.2f+j*0.08f),y+2.1f-j*0.1f,z,0.4f,0.04f,0.04f);
        t3_object_set_rotation(&straw->base,0,0,s*(j-1)*0.4f);
      }
    }
    prop(root,p.box,p.black,x,y+2.45f,z+0.32f,0.25f,0.05f,0.04f);
  }
  if(circus) {
    circus_elephant(root,&p,t->finish_x*.36f,-7.5f,1.1f,0);
    circus_elephant(root,&p,t->finish_x*.36f-6,-10,.68f,1);
    circus_lion(root,&p,t->finish_x*.79f,-7,false);
    circus_lion(root,&p,t->finish_x*.79f+5,-11,true);
    /* Varied footprints, actual cloth stripes, and four distinct palettes. */
    t3_material *cloth[6];
    const float rgb[6][3]={{.74f,.055f,.09f},{.98f,.91f,.73f},{.035f,.16f,.48f},{1,.65f,.08f},{.1f,.48f,.37f},{.43f,.11f,.45f}};
    for(int i=0;i<6;i++){cloth[i]=t3_mesh_standard_material_new(0xffffff);cloth[i]->color=lin(rgb[i][0],rgb[i][1],rgb[i][2]);cloth[i]->roughness=.94f;}
    const float tent_x[]={18,60,108,165,232,294,350,412};
    const float radius[]={4,6,8,4.5f,12,7,4.5f,9};
    const float span[]={0,2.5f,3.5f,0,6,0,1.5f,4};
    const int ca[]={4,2,5,0,0,2,4,5},cb[]={1,3,1,3,1,1,3,3};
    for(int i=0;i<8;i++) {
      float x=tent_x[i],r=radius[i],z=-7-r,y=bank_y(x,z,kind);
      circus_tent(root,&p,x,y,z,r,span[i],i==4?3.5f:3, r*.65f,cloth[ca[i]],cloth[cb[i]],cloth[(i%2)?0:2]);
    }
    for(int i=0;i<6;i++)t3_release(cloth[i]);
    for(int i=0;i<5;i++) {
      float x=37+i*76,z=-4.8f,y=bank_y(x,z,kind);
      circus_clown(root,&p,x,y,z,i);
    }
    for(int i=0;i<44;i++) {
      float x=4+i*8,z=-4.5f,y=bank_y(x,z,kind);
      prop(root,p.cyl,p.gold,x,y+1.45f,z,0.025f,2.9f,0.025f);
      whimsy_bob(prop(root,p.ball,colors[i%5],x,y+3.2f,z,0.45f,0.65f,0.45f),i*0.7f);

    }
  }
  if(candy||toy)fantasy_settlement(root,&p,t,candy);
  if(candy){candy_gumdrops(root,&p,t);chocolate_falls(root,&p,t);}
  t3_material *gummy_jelly[4]={0};
  if(candy) {
    const unsigned hues[]={0xf42b35,0xffbb21,0x32b84b,0xff701f};
    for(int j=0;j<4;j++) {
      gummy_jelly[j]=t3_mesh_physical_material_new(hues[j]);
      gummy_jelly[j]->roughness=.24f;
      gummy_jelly[j]->transmission=.9f;
      gummy_jelly[j]->ior=1.35f;
      gummy_jelly[j]->thickness=.45f;
      gummy_jelly[j]->attenuation_color=t3_color_hex(hues[j]);
      gummy_jelly[j]->attenuation_distance=1.7f;
    }
  }
  t3_geometry *gummy=candy?gummy_geometry():NULL;
  t3_geometry *donut=candy?t3_torus_geometry_new(1,.32f,10,32,6.2831853f):NULL;
  if(candy) for(int i=0;i<62;i++) {
    float x=12+(i/8)*(t->finish_x-35)/8+(i%8)*2.6f+3*cw_sin(i*2.3f);
    const float depths[]={-7,-20,-38,-12,-53,-29,-17,-44};
    float z=depths[i%8]-2*cw_cos(i);
    /* Leave the streams and waterfall visible between the candy buildings. */
    for(int river=0;river<2;river++) {
      float cx=t->finish_x*(river?.73f:.34f),clear=river&&z<-10?13:6;
      if(z>-36&&fabsf(x-cx)<clear)x=cx+(x<cx?-clear:clear);
    }
    float y=bank_y(x,z,kind);
    int first=root->child_count;
    t3_material *sweet=colors[(i*3+i/7)%5];
    int variety=i<10?i%2:i%6; /* Reveal the stranger sweets farther into the course. */
    if(variety==2) { /* Striped candy canes: straight shaft and rounded hook. */
      for(int j=0;j<12;j++)prop(root,p.cyl,j%2?sweet:p.cream,x,y+.15f+j*.28f,z,.23f,.29f,.23f);
      for(int j=0;j<18;j++) {
        float a=j*3.14159265f/17;
        prop(root,p.ball,(j/2)%2?sweet:p.cream,x+.7f-.7f*cw_cos(a),y+3.23f+.7f*cw_sin(a),z,.24f,.24f,.24f);
      }
      for(int j=0;j<3;j++)prop(root,p.cyl,j%2?sweet:p.cream,x+1.4f,y+3.1f-j*.28f,z,.23f,.29f,.23f);
    } else if(variety==3) { /* Donuts with icing and colorful sprinkles. */
      prop(root,donut,p.brown,x,y+1.6f,z,1.4f,1.4f,1.4f);
      prop(root,donut,sweet,x,y+1.6f,z+.22f,1.4f,1.4f,.85f);
      for(int j=0;j<14;j++) {
        float a=j*.44879895f;
        t3_mesh *sprinkle=prop(root,p.box,colors[(i+j)%5],x+cw_cos(a)*1.4f,y+1.6f+cw_sin(a)*1.4f,z+.5f,.22f,.065f,.065f);
        t3_object_set_rotation(&sprinkle->base,0,0,a+.6f);
      }
    } else if(variety==4) { /* One smooth molded sweet, with raised facial details. */
      prop(root,gummy,gummy_jelly[(i/6)%4],x,y,z,1,1,1);
    } else if(variety==5) { /* Tiered layer cakes with alternating fillings. */
      for(int tier=0;tier<3;tier++) {
        float r=1.7f-tier*.4f,h=y+.45f+tier*.85f;
        prop(root,p.cyl,tier%2?p.brown:p.cream,x,h,z,r,.7f,r);
        prop(root,p.cyl,sweet,x,h+.38f,z,r+.06f,.15f,r+.06f);
        for(int j=0;j<8;j++) {
          float a=j*.7853982f;
          prop(root,p.ball,p.cream,x+cw_cos(a)*r*.9f,h+.49f,z+cw_sin(a)*r*.9f,.15f,.14f,.15f);
        }
      }
      prop(root,p.ball,p.red,x,y+2.9f,z,.25f,.25f,.25f);
    } else if(variety==1) {
      prop(root,p.cyl,p.cream,x,y+1.1f,z,0.09f,2.2f,0.09f);
      prop(root,p.ball,colors[i%5],x,y+2.7f,z,1.3f,1.3f,0.35f);
      prop(root,p.ring,p.cream,x,y+2.7f,z+0.33f,0.85f,0.85f,0.35f);
      prop(root,p.ring,p.cream,x,y+2.7f,z+0.36f,0.4f,0.4f,0.35f);
    } else {
      prop(root,p.cyl,p.brown,x,y+1,z,1.6f,2,1.6f);
      prop(root,p.ball,sweet,x,y+2,z,1.8f,0.9f,1.8f);
      prop(root,p.ball,p.red,x,y+2.9f,z,0.3f,0.3f,0.3f);
      for(int j=0;j<8;j++) {
        float a=j*0.7853982f;
        prop(root,p.box,colors[j%5],x+cw_cos(a)*1.3f,y+2.5f,z+cw_sin(a)*1.3f,0.3f,0.12f,0.12f);
      }
    }
    const float sizes[]={.32f,1.15f,.65f,2.4f,.48f,1.7f,3.1f,.85f,.55f,1.35f,2.0f};
    float scale=sizes[(i*7+i/8)%11];
    for(int k=first;k<root->child_count;k++) {
      t3_object *o=root->children[k];
      o->position=(t3_vec3){x+(o->position.x-x)*scale,y+(o->position.y-y)*scale,z+(o->position.z-z)*scale};
      o->scale.x*=scale;o->scale.y*=scale;o->scale.z*=scale;
    }
    if(variety==0) {
      /* Big cupcake bases extend into the slope around their whole footprint,
       * instead of balancing on the centre sample with one edge airborne. */
      float low=bank_surface_y(x,z,kind);
      for(int j=0;j<16;j++) {
        float a=j*6.2831853f/16;
        low=fminf(low,bank_surface_y(x+cw_cos(a)*1.6f*scale,z+cw_sin(a)*1.6f*scale,kind));
      }
      low-=.12f;
      t3_object *base=root->children[first];
      base->position.y=(low+y+2*scale)*.5f;base->scale.y=y+2*scale-low;
    }
  }
  if(donut)t3_release(donut);
  if(gummy)t3_release(gummy);
  for(int j=0;j<4;j++)if(gummy_jelly[j])t3_release(gummy_jelly[j]);
  if(toy) for(int i=0;i<42;i++) {
    float x=14+(i/7)*(t->finish_x-40)/6+(i%7)*3.2f+3*cw_sin(i*1.7f);
    const float depths[]={-9,-26,-46,-17,-36,-57,-22};
    float z=depths[i%7],y=bank_y(x,z,kind);
    int first=root->child_count;
    int type=i<7?i%2:i%6;
    t3_material *paint=colors[(i+i/7)%5],*accent=colors[(i+2)%5];
    if(type==2) { /* Wooden propeller plane on three little wheels. */
      prop(root,p.ball,paint,x,y+1.1f,z,1.8f,.4f,.42f);
      prop(root,p.box,accent,x+.1f,y+1.03f,z,1.25f,.16f,4.1f);
      prop(root,p.box,accent,x-1.35f,y+1.12f,z,.65f,.12f,1.6f);
      prop(root,p.box,accent,x-1.35f,y+1.47f,z,.65f,.8f,.12f);
      prop(root,p.ball,p.glass,x+.35f,y+1.46f,z,.55f,.23f,.3f);
      prop(root,p.box,p.cream,x+1.85f,y+1.12f,z,.12f,1.5f,.12f);
      prop(root,p.ball,p.gold,x+1.95f,y+1.12f,z,.18f,.18f,.18f);
      for(int side=-1;side<=1;side+=2) {
        prop(root,p.box,p.gold,x+.65f,y+.55f,z+side*.6f,.12f,.7f,.12f);
        prop(root,p.ball,p.black,x+.65f,y+.25f,z+side*.6f,.24f,.24f,.13f);
      }
      prop(root,p.ball,p.black,x-1.25f,y+.5f,z,.15f,.15f,.12f);
    } else if(type==3) { /* Retro rocket with contrasting nose, fins and porthole. */
      prop(root,p.cyl,paint,x,y+1.8f,z,.65f,2.6f,.65f);
      prop(root,p.cone,accent,x,y+3.65f,z,.67f,1.1f,.67f);
      prop(root,p.cyl,p.cream,x,y+2.7f,z,.68f,.2f,.68f);
      for(int side=-1;side<=1;side+=2) {
        t3_mesh *fin=prop(root,p.box,accent,x+side*.65f,y+.7f,z,.4f,1.3f,.8f);
        t3_object_set_rotation(&fin->base,0,0,side*.35f);
      }
      prop(root,p.ball,p.cream,x,y+2.3f,z+.58f,.33f,.33f,.1f);
      prop(root,p.ball,p.glass,x,y+2.3f,z+.68f,.24f,.24f,.07f);
      prop(root,p.cyl,p.black,x,y+.45f,z,.4f,.45f,.4f);
    } else if(type==4) { /* Rubber duck: low tail, round chest, flattened bill. */
      t3_material *rubber=i%3? p.gold:paint;
      prop(root,p.ball,rubber,x,y+.65f,z,1.05f,.65f,.7f);
      prop(root,p.ball,rubber,x+.65f,y+1.45f,z,.58f,.65f,.52f);
      prop(root,p.ball,p.red,x+1.16f,y+1.35f,z,.46f,.13f,.38f);
      prop(root,p.cone,rubber,x-.95f,y+.75f,z,.4f,.65f,.4f);
      for(int side=-1;side<=1;side+=2) {
        prop(root,p.ball,rubber,x-.15f,y+.85f,z+side*.57f,.65f,.35f,.17f);
        prop(root,p.ball,p.black,x+.85f,y+1.63f,z+side*.44f,.07f,.08f,.04f);
      }
    } else if(type==5) { /* Oversized dice: recessed-looking dark pips on two faces. */
      prop(root,p.box,i%2?p.cream:paint,x,y+1,z,2,2,2);
      const float pips[6][2]={{-.5f,-.5f},{-.5f,0},{-.5f,.5f},{.5f,-.5f},{.5f,0},{.5f,.5f}};
      for(int k=0;k<6;k++) {
        prop(root,p.ball,p.black,x+pips[k][0],y+1+pips[k][1],z+1.01f,.12f,.12f,.025f);
        if(k%2==0)prop(root,p.ball,p.black,x+1.01f,y+1+pips[k][0],z+pips[k][1],.025f,.12f,.12f);
      }
    } else if(type==1) { /* smiling wind-up robots */
      prop(root,p.box,colors[i%5],x,y+1.8f,z,1.8f,2.1f,1.1f);
      prop(root,p.box,p.cream,x,y+3.3f,z,1.7f,1.1f,1.2f);
      for(int s=-1;s<=1;s+=2) {
        prop(root,p.ball,p.blue,x+s*0.4f,y+3.4f,z+0.62f,0.2f,0.22f,0.1f);
        prop(root,p.box,p.blue,x+s*0.6f,y+0.45f,z+0.15f,0.55f,0.8f,1.2f);
        prop(root,p.ball,p.gold,x+s*1.2f,y+2.2f,z,0.4f,0.4f,0.4f);
        prop(root,p.ring,p.gold,x+s*0.3f,y+4.4f,z,0.4f,0.4f,0.4f);
      }
      prop(root,p.box,p.black,x,y+3.05f,z+0.63f,0.65f,0.12f,0.04f);
      prop(root,p.cyl,p.gold,x,y+3.9f,z,0.08f,0.7f,0.08f);
      prop(root,p.ball,p.red,x,y+1.8f,z+0.6f,0.3f,0.3f,0.1f);
    } else for(int j=0;j<3;j++) {
      t3_mesh *block=prop(root,p.box,colors[(i+j)%5],x+(j==2?0.2f:j-0.5f)*1.7f,y+(j==2?2.8f:0.9f),z,1.8f,1.8f,1.8f);
      t3_object_set_rotation(&block->base,0,(j-1)*0.15f,0);
      prop(root,p.ball,p.cream,x+(j==2?0.2f:j-0.5f)*1.7f,y+(j==2?2.8f:0.9f),z+0.96f,0.32f,0.32f,0.06f);
    }
    const float sizes[]={.4f,1.7f,.8f,2.7f,1.1f,.6f,2.1f};
    float scale=sizes[(i*3+i/7)%7];
    for(int k=first;k<root->child_count;k++) {
      t3_object *o=root->children[k];
      o->position=(t3_vec3){x+(o->position.x-x)*scale,y+(o->position.y-y)*scale,z+(o->position.z-z)*scale};
      o->scale.x*=scale;o->scale.y*=scale;o->scale.z*=scale;
    }
  }
  if(candy) for(int i=0;i<85;i++) {
    float x=18+(i/7)*32+(i%7)*.8f,z=-6-(i/7%5)*7-(i%3)*.65f,y=bank_y(x,z,kind);
    prop(root,p.ball,colors[i%5],x,y+0.18f,z,0.35f,0.4f,0.35f);
  }
  if(city) {
    p.glass->emissive=lin(1,0.66f,0.3f);
    p.glass->emissive_intensity=0.025f;
    p.glass->color=lin(.14f,.2f,.23f);
    p.glass->metalness=.35f;
    p.glass->roughness=.22f;
    p.pink->emissive=lin(1,0.15f,0.5f);
    p.pink->emissive_intensity=1.2f;
    t3_material *asphalt=mat(TEX_ASPHALT,(float[3]){0.36f,0.38f,0.43f},1,1,0.18f,0.78f);
    t3_material *concrete=mat(TEX_CONCRETE,(float[3]){0.65f,0.67f,0.72f},1,1,0.5f,0.9f);
    /* Asphalt riding stretches alternate with concrete roof decks. Sample
     * the exact physics ribbon so even curved sections have no floating slabs. */
    mb road={0};
    for(int pc=0;pc<t->piece_count;pc++) {
      const track_piece *piece=&t->pieces[pc];
      for(int j=1;j<piece->count;j++) {
        float x0=piece->pts[(j-1)*2], y0=piece->pts[(j-1)*2+1]+0.008f;
        float x1=piece->pts[j*2], y1=piece->pts[j*2+1]+0.008f;
        if(x0<0 || ((int)x0%70)>=44) continue;
        t3_vec3 normal={y0-y1,x1-x0,0};
        int a=mb_v(&road,(t3_vec3){x0,y0,-TRACK_HALF_W},normal,x0/3,0,1);
        mb_v(&road,(t3_vec3){x0,y0,TRACK_HALF_W-0.18f},normal,x0/3,1,1);
        mb_v(&road,(t3_vec3){x1,y1,TRACK_HALF_W-0.18f},normal,x1/3,1,1);
        mb_v(&road,(t3_vec3){x1,y1,-TRACK_HALF_W},normal,x1/3,0,1);
        mb_t(&road,a,a+1,a+2);mb_t(&road,a,a+2,a+3);
      }
    }
    t3_geometry *road_geo=mb_done(&road);
    add(root,road_geo,asphalt,false);
    t3_release(road_geo);
    /* Continuous street follows the smoothed city grade, with sidewalk and dashed centreline. */
    mb street={0};
    for(float x=-24;x<t->length+20;x+=2) {
      float y=bank_y(x,-7,2);
      int first=root->child_count;
      /* Adjacent cross-sections share positions exactly: overlapping boxes
       * left depth-fighting seams and drew unseen sides/bottoms. */
      float ya=bank_y(x-1,-7,2)+.09f,yb=bank_y(x+1,-7,2)+.09f;
      t3_vec3 normal={-(yb-ya)*.5f,1,0};
      int a=mb_v(&street,(t3_vec3){x-1,ya,-10.5f},normal,(x-1)/3,0,1);
      mb_v(&street,(t3_vec3){x-1,ya,-3.5f},normal,(x-1)/3,7.0f/3,1);
      mb_v(&street,(t3_vec3){x+1,yb,-3.5f},normal,(x+1)/3,7.0f/3,1);
      mb_v(&street,(t3_vec3){x+1,yb,-10.5f},normal,(x+1)/3,0,1);
      mb_t(&street,a,a+1,a+2);mb_t(&street,a,a+2,a+3);
      prop(root,p.box,concrete,x,y+0.18f,-11.2f,2.05f,0.4f,1.4f);
      if(((int)x/2)%3==0) prop(root,p.box,p.gold,x,y+0.1f,-7,1.3f,0.025f,0.1f);
      float grade=cw_atan2(bank_y(x+1,-7,2)-bank_y(x-1,-7,2),2);
      for(int k=first;k<root->child_count;k++)t3_object_set_rotation(root->children[k],0,0,grade);
      /* Keep separate 24 m chunks for the city visibility manager. */
      if(((int)x+24)%24==22 || x+2>=t->length+20) {
        t3_geometry *street_geo=mb_done(&street);
        add(root,street_geo,asphalt,false);t3_release(street_geo);
      }
    }
    city_cars(root,&p);
    for(int i=0;i<18;i++) {
      float x=8+i*18,z=-15,y=bank_y(x,z,2);
      if(mesa_frontage_reserved(x)) continue;
      prop(root,p.box,colors[i%5],x,y+2.2f,z,7,4.4f,5);
      prop(root,p.box,p.glass,x,y+1.8f,z+2.55f,5.8f,2.3f,0.08f);
      prop(root,p.box,p.black,x+2.1f,y+1.35f,z+2.62f,1.1f,2.7f,0.1f);
      for(int j=0;j<8;j++) prop(root,p.box,j%2?p.cream:colors[i%5],x-3.1f+j*0.88f,y+3.2f,z+2.9f,0.88f,0.25f,1.4f);
      prop(root,p.box,p.black,x,y+4.1f,z+2.6f,4,0.9f,0.1f);
      for(int j=0;j<4;j++) prop(root,p.box,p.cream,x-2.8f+j*1.4f,y+1.8f,z+2.65f,0.08f,2.3f,0.08f);
      prop(root,p.box,p.cream,x,y+0.65f,z+2.65f,5.8f,0.12f,0.08f);
      /* Shop signs: a donut, a toy bicycle, or glowing barber stripes. */
      if(i%3==0) prop(root,p.ring,p.pink,x,y+4.15f,z+2.75f,0.6f,0.6f,0.4f);
      else if(i%3==1) {
        for(int s=-1;s<=1;s+=2) prop(root,p.ring,p.lamp,x+s*0.65f,y+4.1f,z+2.75f,0.38f,0.38f,0.3f);
        prop(root,p.box,p.red,x,y+4.2f,z+2.78f,1.2f,0.12f,0.1f);
      } else for(int j=0;j<5;j++) prop(root,p.box,j%2?p.blue:p.red,x-1.3f+j*0.65f,y+4.1f,z+2.75f,0.35f,0.6f,0.1f);
    }
    float lamp_x[64];int lamp_count=mesa_streetlight_positions(lamp_x);
    for(int lamp=0;lamp<lamp_count;lamp++) {
      float x=lamp_x[lamp],y=bank_y(x,-10.7f,2);
      prop(root,p.cyl,p.black,x,y+2.4f,-10.7f,0.07f,4.8f,0.07f);
      prop(root,p.box,p.black,x,y+4.85f,-10.1f,0.1f,0.1f,1.3f);
      prop(root,p.ball,p.lamp,x,y+4.65f,-9.5f,0.22f,0.15f,0.22f);
      t3_light *street=t3_point_light_new(0xffcd91,110,17,2);
      street->base.position=(t3_vec3){x,y+4.65f,-9.5f};
      t3_object_add(root,&street->base);t3_release(street);
      if(((int)x/18)%3==0) for(int j=0;j<6;j++) prop(root,p.box,p.cream,x,y+0.12f,-9.5f+j,1.7f,0.025f,0.45f);
    }
    mesa_buildings(root,&p);
    /* Fixed neon spill lights: clustered shading pays only where they reach. */
    const float signs[]={220,266,190,112};
    const unsigned colors[]={0xff3399,0xff438e,0x52ff91,0xffbc4c};
    for(int i=0;i<4;i++) {
      float x=signs[i];
      t3_light *spill=t3_point_light_new(colors[i],i==2?28:55,9,2);
      spill->base.position=(t3_vec3){x,bank_y(x,-14.5f,2)+3.5f,-10.6f};
      t3_object_add(root,&spill->base);t3_release(spill);
    }
    t3_release(asphalt);t3_release(concrete);
  }
  t3_geometry *gs[]={p.box,p.ball,p.cone,p.cyl,p.ring};
  t3_material *ms[]={p.red,p.cream,p.gold,p.blue,p.green,p.pink,p.brown,p.black,p.glass,p.lamp};
  for(int i=0;i<5;i++) t3_release(gs[i]);
  for(int i=0;i<10;i++) t3_release(ms[i]);
}

static t3_object *loose_toy_nodes[LOOSE_TOYS];
static t3_instanced_mesh *pit_balls[4];
static int32_t pit_matrix_updates;
static void loose_toys_build(t3_object *root,const track_desc *t) {
  memset(loose_toy_nodes,0,sizeof loose_toy_nodes);memset(pit_balls,0,sizeof pit_balls);
  pit_matrix_updates=0;cw_debug_i32("toys.pit_matrix_updates",&pit_matrix_updates);
  if(strcmp(t->theme,"toybox"))return;
  t3_geometry *box=t3_box_geometry_new(1,1,1,1,1,1),*ball=t3_sphere_geometry_new(1,12,8);
  t3_geometry *ring=t3_torus_geometry_new(.277f,.008f,5,20,6.2831853f);
  t3_material *colors[8]={toy_paint(.95f,.13f,.12f),toy_paint(.12f,.5f,.95f),toy_paint(1,.76f,.08f),toy_paint(.15f,.85f,.4f),toy_paint(.95f,.92f,.8f),toy_paint(.035f,.025f,.02f),toy_paint(.6f,.29f,.1f),toy_paint(.95f,.37f,.05f)};
  colors[6]->roughness=.95f;colors[6]->clearcoat=0; /* plush */
  for(int i=0;i<TOY_PIT_START;i++) {
    t3_object *g=t3_group_new();t3_object_add(root,g);loose_toy_nodes[i]=g;
    int kind=crash_toy_kind(i);float scale=crash_toy_size(i);g->scale=(t3_vec3){scale,scale,scale};
    if(kind==0) { /* soft teddy, round ears, short limbs, contrasting muzzle */
      prop(g,ball,colors[6],0,0,0,.24f,.29f,.18f);
      prop(g,ball,colors[6],0,.32f,0,.22f,.21f,.19f);
      for(int side=-1;side<=1;side+=2) {
        prop(g,ball,colors[6],side*.16f,.49f,0,.09f,.09f,.075f);
        prop(g,ball,colors[6],side*.25f,.03f,.02f,.12f,.18f,.12f);
        prop(g,ball,colors[6],side*.13f,-.21f,.1f,.12f,.11f,.16f);
        prop(g,ball,colors[5],side*.075f,.36f,.17f,.025f,.025f,.02f);
      }
      prop(g,ball,colors[4],0,.27f,.17f,.12f,.08f,.065f);
      prop(g,ball,colors[5],0,.3f,.23f,.035f,.025f,.02f);
    } else if(kind==1||kind==6) {
      prop(g,box,colors[kind==1?4:(i/3)%4],0,0,0,.48f,.48f,.48f);
      if(kind==1)for(int j=-1;j<=1;j++)for(int k=-1;k<=1;k++) {
        if((j==0)!=(k==0))continue;
        prop(g,ball,colors[5],j*.14f,k*.14f,.242f,.034f,.034f,.009f);
        prop(g,ball,colors[5],.242f,j*.14f,k*.14f,.009f,.034f,.034f);
        prop(g,ball,colors[5],j*.14f,.242f,k*.14f,.034f,.009f,.034f);
      }
      else for(int j=-1;j<=1;j+=2)for(int k=-1;k<=1;k+=2)
        prop(g,ball,colors[(i/3)%4],j*.13f,.245f,k*.13f,.075f,.04f,.075f);
    } else if(kind==8) { /* yellow rubber chicken, stretched neck and red wattle */
      prop(g,ball,colors[2],0,0,0,.18f,.29f,.16f);
      prop(g,ball,colors[2],0,.28f,0,.07f,.25f,.075f);
      prop(g,ball,colors[2],0,.49f,.015f,.12f,.13f,.11f);
      prop(g,ball,colors[7],0,.47f,.14f,.07f,.045f,.09f);
      prop(g,ball,colors[0],0,.37f,.1f,.045f,.08f,.04f);
      for(int j=-1;j<=1;j+=2) {
        prop(g,ball,colors[5],j*.065f,.53f,.095f,.018f,.02f,.018f);
        prop(g,ball,colors[7],j*.09f,-.25f,.05f,.065f,.04f,.12f);
      }
      for(int j=0;j<3;j++)prop(g,ball,colors[0],0,.61f,.06f-j*.055f,.035f,.055f,.04f);
    } else {
      prop(g,ball,colors[kind==3?7:kind==5?4:kind==4?0:1],0,0,0,.28f,.28f,.28f);
      if(kind==2)for(int j=0;j<4;j++) {
        t3_mesh *stripe=prop(g,ball,colors[j],0,0,0,.281f,.281f,.11f);
        t3_object_set_rotation(&stripe->base,0,j*.785398f,0);
      }
      if(kind==3||kind==4||kind==7)for(int j=0;j<3;j++) {
        t3_mesh *seam=prop(g,ring,colors[kind==7?2:5],0,0,0,1,1,1);
        t3_object_set_rotation(&seam->base,j==1?1.570796f:0,j==2?1.570796f:0,0);
      }
      if(kind==5)for(int j=0;j<12;j++) {
        float yy=1-2*(j+.5f)/12,rr=sqrtf(1-yy*yy),a=j*2.399963f;
        t3_vec3 n={rr*cw_cos(a),yy,rr*cw_sin(a)};
        prop(g,ball,colors[5],n.x*.23f,n.y*.23f,n.z*.23f,.09f,.09f,.09f);
      }
    }
    merge_still(g,0,g->child_count,false);
    t3_release(g);
  }
  for(int c=0;c<4;c++) {
    pit_balls[c]=t3_instanced_mesh_new(ball,colors[c],TOY_PIT_BALLS/4);
    pit_balls[c]->instance_matrix->usage=T3_DYNAMIC_DRAW;
    pit_balls[c]->mesh.base.cast_shadow=true;pit_balls[c]->mesh.base.receive_shadow=true;
    t3_object_add(root,pit_balls[c]);t3_release(pit_balls[c]);
  }
  t3_material *plex=t3_mesh_physical_material_new(0xffffff);
  plex->roughness=.06f;plex->transmission=.98f;plex->thickness=.08f;plex->ior=1.49f;
  float px=t->finish_x+9,py=track_height_at(px,NULL);
  prop(root,box,colors[1],px,py-.18f,0,9,.36f,5.6f);
  for(int side=-1;side<=1;side+=2) {
    prop(root,box,plex,px,py+1.35f,side*2.7f,9,2.7f,.08f);
    prop(root,box,colors[2],px,py+2.72f,side*2.7f,9,.18f,.42f);
  }
  for(int end=-1;end<=1;end+=2)prop(root,box,plex,px+end*4.3f,py+1.35f,0,.08f,2.7f,5.6f);
  /* Bright padded springboard explains the post-finish launch into the pit. */
  prop(root,box,colors[1],t->finish_x+1,py+.1f,0,2,.2f,3.6f);
  prop(root,box,colors[2],t->finish_x+1,py+.24f,0,1.8f,.12f,3.4f);
  t3_release(plex);
  t3_release(box);t3_release(ball);t3_release(ring);for(int i=0;i<8;i++)t3_release(colors[i]);
}
static void loose_toys_frame(float alpha) {
  bool dirty[4]={false};pit_matrix_updates=0;
  for(int i=TOY_PIT_START;i<crash_toys_count();i++) {
    cw_xform3 x=crash_toy_pose(i,alpha);int n=i-TOY_PIT_START;
    int color=(n+(n/4)*7+(n/12))%4;
    t3_instanced_mesh *im=pit_balls[color];
    const float *old=t3_attribute_f32(im->instance_matrix)+(n/4)*16;
    /* Uniform unmarked spheres need translation only. Compare the rendered
     * pose, not sleep state, so the last interpolated step is never dropped. */
    if(old[12]==x.p.x&&old[13]==x.p.y&&old[14]==x.p.z&&old[0]==.28f)continue;
    t3_mat4 m={{.28f,0,0,0,0,.28f,0,0,0,0,.28f,0,x.p.x,x.p.y,x.p.z,1}};
    t3_instanced_mesh_set_matrix_at(im,n/4,&m);dirty[color]=true;pit_matrix_updates++;
  }
  /* Sleeping piles retain their GPU buffers; impacts publish changed batches. */
  for(int c=0;c<4;c++)if(dirty[c])pit_balls[c]->instance_matrix->version++;
  for(int i=0;i<TOY_PIT_START&&i<crash_toys_count();i++)if(loose_toy_nodes[i]) {
    cw_xform3 x=crash_toy_pose(i,alpha);loose_toy_nodes[i]->position=x.p;t3_object_set_quaternion(loose_toy_nodes[i],x.q);
  }
}

/* Separate query meshes preserve tight prop bounds after render batching.
 * Geometry/material storage is shared; these copies are never rendered. */
typedef struct {t3_object *object;t3_vec3 center;float radius;} chocolate_collider;
static chocolate_collider *choc_colliders;
static int choc_collider_count;
static t3_raycaster *choc_ray;
static int32_t choc_scenery_hits;
static void chocolate_colliders_build(t3_object *root,int first,const track_desc *t) {
  for(int i=0;i<choc_collider_count;i++)t3_release(choc_colliders[i].object);
  free(choc_colliders);choc_colliders=NULL;choc_collider_count=0;
  if(choc_ray)t3_release(choc_ray);choc_ray=NULL;choc_scenery_hits=0;
  cw_debug_i32("chocolate.scenery_hits",&choc_scenery_hits);
  if(strcmp(t->theme,"candy"))return;
  choc_colliders=calloc(root->child_count-first,sizeof *choc_colliders);
  choc_ray=t3_raycaster_new();
  for(int i=first;i<root->child_count;i++) {
    t3_object *o=root->children[i];if(o->type!=T3_MESH||!o->visible)continue;
    bool moving=false;for(int j=0;j<whimsy_motion_count;j++)if(whimsy_motion_list[j].object==o)moving=true;
    if(moving)continue;
    t3_mesh *m=(t3_mesh *)o;t3_geometry *g=m->geometry;
    if(!g->has_bounding_sphere)t3_geometry_compute_bounding_sphere(g);
    t3_object_update_matrix(o);
    t3_vec3 center=t3_vec3_apply_mat4(g->bounding_sphere.center,&o->matrix);
    float radius=g->bounding_sphere.radius*fmaxf(fabsf(o->scale.x),fmaxf(fabsf(o->scale.y),fabsf(o->scale.z)));
    if(fabsf(center.z)>radius+14)continue;
    if(fabsf(center.x-t->finish_x*.34f)>radius+24&&fabsf(center.x-t->finish_x*.73f)>radius+24)continue;
    t3_mesh *copy=t3_mesh_new(g,m->materials[0]);
    copy->base.position=o->position;copy->base.scale=o->scale;t3_object_set_quaternion(copy,o->quaternion);
    t3_object_update_world_matrix(&copy->base,false,false);
    choc_colliders[choc_collider_count++]=(chocolate_collider){&copy->base,center,radius};
  }
}
static bool chocolate_scenery_contact(t3_vec3 origin,t3_vec3 delta,cw_ray_hit3d *hit) {
  float length=t3_vec3_length(delta);if(!choc_ray||length<.00001f)return false;
  t3_vec3 direction=t3_vec3_scale(delta,1/length);
  t3_raycaster_set(choc_ray,origin,direction);choc_ray->near=.00001f;
  bool found=false;
  for(int i=0;i<choc_collider_count;i++) {
    chocolate_collider *c=&choc_colliders[i];
    float far=length*hit->fraction;
    t3_vec3 v=t3_vec3_sub(c->center,origin);
    float along=CW_CLAMP(t3_vec3_dot(v,direction),0,far);
    t3_vec3 separation=t3_vec3_sub(v,t3_vec3_scale(direction,along));
    if(t3_vec3_dot(separation,separation)>c->radius*c->radius)continue;
    choc_ray->far=far;const t3_intersection *hits;
    if(!t3_raycaster_intersect_objects(choc_ray,&c->object,1,false,&hits))continue;
    const t3_intersection *h=&hits[0];
    t3_mesh *m=(t3_mesh *)h->object;const float *pos=m->geometry->attributes[T3_ATTR_POSITION]->array;
    t3_vec3 a={pos[h->a*3],pos[h->a*3+1],pos[h->a*3+2]};
    t3_vec3 b={pos[h->b*3],pos[h->b*3+1],pos[h->b*3+2]};
    t3_vec3 cpt={pos[h->c*3],pos[h->c*3+1],pos[h->c*3+2]};
    a=t3_vec3_apply_mat4(a,&m->base.matrix_world);b=t3_vec3_apply_mat4(b,&m->base.matrix_world);cpt=t3_vec3_apply_mat4(cpt,&m->base.matrix_world);
    t3_vec3 normal=t3_vec3_normalize(t3_vec3_cross(t3_vec3_sub(b,a),t3_vec3_sub(cpt,a)));
    if(t3_vec3_dot(normal,direction)>0)normal=t3_vec3_scale(normal,-1);
    hit->point=h->point;hit->normal=normal;hit->fraction=h->distance/length;found=true;
  }
  return found;
}
static t3_object *choc_drops[MAX_RIDERS][24];
static bool choc_drop_done[MAX_RIDERS][24];
static cw_node chocolate_parent;
static t3_vec3 choc_splash_at[MAX_RIDERS];
static float choc_splash_age[MAX_RIDERS];
static bool choc_splashed[MAX_RIDERS];
static int32_t choc_splash_count, choc_body_hits, choc_ground_hits;
static float choc_splash_power[MAX_RIDERS], choc_splash_vx[MAX_RIDERS];
static bool choc_crash_coated[MAX_RIDERS];
/* Fixed pool of short-lived impact flecks; persistent deposits use existing decals. */
typedef struct {t3_object *object;t3_vec3 velocity;float age;} chocolate_spray;
static chocolate_spray choc_spray[64], choc_bubbles[16];
static const track_desc *choc_bubble_track;
static int32_t choc_bubble_hits, choc_bubble_emitted, choc_bubble_body_hits;
static int choc_bubble_tick;
static int choc_spray_next;
/* Persistent coverage, not an ever-growing stack of coincident splats. Two
 * deposits per 40cm cell/face; each river retains its own 512-deposit budget.
 * Never recycle existing marks: they remain for the race, including multiplayer. */
typedef struct {int x,y,z;unsigned char face,count;} chocolate_cell;
static chocolate_cell choc_cells[2048];
static int32_t choc_deposits, choc_redundant;
static int choc_river_deposits[2];
static bool chocolate_deposit(t3_vec3 point,t3_vec3 normal) {
  int river=fabsf(point.x-choc_bubble_track->finish_x*.73f)<fabsf(point.x-choc_bubble_track->finish_x*.34f);
  if(choc_river_deposits[river]>=512){choc_redundant++;return false;}
  int x=(int)floorf(point.x*2.5f),y=(int)floorf(point.y*2.5f),z=(int)floorf(point.z*2.5f);
  int axis=fabsf(normal.x)>fabsf(normal.y)?0:1;
  if(fabsf(normal.z)>(axis?fabsf(normal.y):fabsf(normal.x)))axis=2;
  float direction=axis==0?normal.x:axis==1?normal.y:normal.z;
  unsigned face=axis*2+(direction<0);
  unsigned hash=((uint32_t)x*73856093u)^((uint32_t)y*19349663u)^((uint32_t)z*83492791u)^face;
  for(int probe=0;probe<2048;probe++) {
    chocolate_cell *c=&choc_cells[(hash+probe)&2047];
    if(!c->count){*c=(chocolate_cell){x,y,z,face,1};}
    else if(c->x==x&&c->y==y&&c->z==z&&c->face==face) {
      if(c->count>=2){choc_redundant++;return false;}
      c->count++;
    } else continue;
    choc_deposits++;choc_river_deposits[river]++;return true;
  }
  choc_redundant++;return false;
}
static void chocolate_burst(cw_ray_hit3d hit) {
  for(int j=0;j<4;j++) {
    chocolate_spray *s=&choc_spray[choc_spray_next++%64];if(!s->object)continue;
    float a=choc_spray_next*2.399963f;
    t3_vec3 v={cw_cos(a),.5f+j*.2f,cw_sin(a)};
    float dot=v.x*hit.normal.x+v.y*hit.normal.y+v.z*hit.normal.z;
    if(dot<0){v.x-=2*dot*hit.normal.x;v.y-=2*dot*hit.normal.y;v.z-=2*dot*hit.normal.z;}
    s->velocity=(t3_vec3){v.x*1.8f,v.y*1.8f,v.z*1.8f};s->age=0;
    s->object->position=(t3_vec3){hit.point.x+hit.normal.x*.06f,hit.point.y+hit.normal.y*.06f,hit.point.z+hit.normal.z*.06f};
    s->object->visible=true;
  }
}
static void chocolate_fx_build(t3_object *root,const track_desc *t) {
  memset(choc_drop_done,0,sizeof choc_drop_done);memset(choc_drops,0,sizeof choc_drops);memset(choc_splashed,0,sizeof choc_splashed);
  choc_splash_count=choc_body_hits=choc_ground_hits=0;
  memset(choc_spray,0,sizeof choc_spray);choc_spray_next=0;
  memset(choc_bubbles,0,sizeof choc_bubbles);
  memset(choc_cells,0,sizeof choc_cells);memset(choc_river_deposits,0,sizeof choc_river_deposits);
  choc_deposits=choc_redundant=0;
  cw_debug_i32("chocolate.deposits",&choc_deposits);
  cw_debug_i32("chocolate.redundant",&choc_redundant);
  choc_bubble_track=t;choc_bubble_tick=choc_bubble_hits=choc_bubble_emitted=choc_bubble_body_hits=0;
  cw_debug_i32("chocolate.bubble_hits",&choc_bubble_hits);
  cw_debug_i32("chocolate.bubble_body_hits",&choc_bubble_body_hits);
  cw_debug_i32("chocolate.bubbles",&choc_bubble_emitted);
  memset(choc_crash_coated,0,sizeof choc_crash_coated);
  cw_debug_i32("chocolate.splashes",&choc_splash_count);
  cw_debug_i32("chocolate.body_hits",&choc_body_hits);
  cw_debug_i32("chocolate.ground_hits",&choc_ground_hits);
  if(strcmp(t->theme,"candy"))return;
  t3_geometry *geo=t3_sphere_geometry_new(1,8,6);
  t3_material *mat=toy_paint(.24f,.065f,.018f);mat->roughness=.26f;
  for(int r=0;r<MAX_RIDERS;r++)for(int i=0;i<24;i++) {
    t3_object *g=t3_group_new();t3_object_add(root,g);choc_drops[r][i]=g;
    prop(g,geo,mat,0,0,0,1,1,1);g->visible=false;t3_release(g);
  }
  for(int i=0;i<64;i++) {
    t3_mesh *m=prop(root,geo,mat,0,0,0,.035f,.055f,.035f);
    choc_spray[i].object=&m->base;m->base.visible=false;
  }
  for(int i=0;i<16;i++) {
    t3_mesh *m=prop(root,geo,mat,0,0,0,.13f,.13f,.13f);
    choc_bubbles[i].object=&m->base;m->base.visible=false;
  }
  t3_release(geo);t3_release(mat);
}
static void chocolate_fx_frame(void) {
  /* Deterministic, rider-triggered river bubbles. Keep a separate pool so an
   * impact burst cannot overwrite the bubble currently being integrated. */
  if(choc_bubbles[0].object&&++choc_bubble_tick%12==0) {
    for(int river=0;river<2;river++) {
      float center=choc_bubble_track->finish_x*(river?.73f:.34f);
      if(!bike_rider_near(center,18))continue;
      chocolate_spray *b=&choc_bubbles[choc_bubble_emitted%16];
      if(b->object->visible)continue;
      float a=choc_bubble_emitted*2.399963f;
      float x=center+cw_sin(a)*1.5f,z=cw_cos(a*1.7f)*1.4f;
      b->object->position=(t3_vec3){x,chocolate_surface(x,z)+.16f,z};
      b->velocity=(t3_vec3){cw_cos(a)*1.5f,4+(choc_bubble_emitted%4)*.6f,-z*.9f};
      b->age=0;b->object->visible=true;choc_bubble_emitted++;
    }
  }
  for(int r=0;r<MAX_RIDERS;r++)if(choc_splashed[r]||(r==0&&choc_spray[0].object)) {
    float t=(choc_splash_age[r]+=1.f/60);
    for(int i=0;i<(r==0?104:24);i++) {
      bool bubble=i>=88;
      chocolate_spray *spray=bubble?&choc_bubbles[i-88]:i>=24?&choc_spray[i-24]:NULL;
      t3_object *o=spray?spray->object:choc_drops[r][i];if(!o)continue;
      if(spray){spray->age+=1.f/60;if(spray->age>(bubble?2.2f:.7f))o->visible=false;}
      else o->visible=choc_splashed[r]&&t<2.2f&&!choc_drop_done[r][i];
      if(!o->visible)continue;
      float a=i*2.399963f,speed=(2+(i%5)*.75f)*choc_splash_power[r];
      t3_vec3 start=choc_splash_at[r];
      start.x+=cw_cos(a)*.42f;start.z+=cw_sin(a)*.42f;
      t3_vec3 next={start.x+(choc_splash_vx[r]+cw_cos(a)*speed)*t,start.y+(3+(i%4))*choc_splash_power[r]*t-5*t*t,start.z+cw_sin(a)*speed*t};
      t3_vec3 prev=t<.02f?start:o->position;
      if(spray) {
        prev=o->position;spray->velocity.y-=10.f/60;
        next=(t3_vec3){prev.x+spray->velocity.x/60,prev.y+spray->velocity.y/60,prev.z+spray->velocity.z/60};
      }
      cw_ray_hit3d hit={0};hit.fraction=1;
      cw_phys_filter filter={0};filter.layers=(1u<<L3_SCENE)|(1u<<L3_RIDER)|(1u<<L3_BIKE)|(1u<<L3_HAND);
      t3_vec3 delta={next.x-prev.x,next.y-prev.y,next.z-prev.z};
      bool contact=cw_world3d_raycast(crash_world(),prev,delta,&filter,&hit);
      if(!contact)hit.fraction=1;
      int rider=contact?bike_rider_for_body(hit.body):-1;
      int upright=bike_chocolate_raycast(prev,delta,&hit);
      if(upright>=0){rider=upright;contact=true;}
      if(chocolate_scenery_contact(prev,delta,&hit)){rider=-1;contact=true;choc_scenery_hits++;}
      /* The decorative banks have no Box3D bodies. Catch drops there too. */
      if(!contact&&fabsf(next.z)>TRACK_HALF_W&&prev.y>bank_surface_y(prev.x,prev.z,0)&&next.y<=bank_surface_y(next.x,next.z,0)) {
        float lo=0,hi=1;
        for(int j=0;j<7;j++){float f=(lo+hi)*.5f;
          if(prev.y+delta.y*f>bank_surface_y(prev.x+delta.x*f,prev.z+delta.z*f,0))lo=f;else hi=f;}
        hit.point=(t3_vec3){prev.x+delta.x*hi,prev.y+delta.y*hi,prev.z+delta.z*hi};
        float x=hit.point.x,z=hit.point.z;
        t3_vec3 n={(bank_surface_y(x-.1f,z,0)-bank_surface_y(x+.1f,z,0))*5,1,(bank_surface_y(x,z-.1f,0)-bank_surface_y(x,z+.1f,0))*5};
        float len=sqrtf(n.x*n.x+n.y*n.y+n.z*n.z);hit.normal=(t3_vec3){n.x/len,n.y/len,n.z/len};contact=true;
      }
      if(contact) {
        if(!spray)choc_drop_done[r][i]=true;
        if(!spray||bubble)chocolate_burst(hit);
        if(bubble)choc_bubble_hits++;
        o->visible=false;
        if(rider>=0){models_chocolate_hit(rider,hit.point,hit.normal);choc_body_hits++;if(bubble)choc_bubble_body_hits++;}
        else {
          choc_ground_hits++;
          if(!chocolate_deposit(hit.point,hit.normal))continue;
          cw_node d=cw_node_new("Decal3D","Chocolate droplet splat");cw_node_add_child(chocolate_parent,d);
          cw_node_set(d,"blend",cw_val_enum("MIX"));cw_node_set(d,"color",cw_val_color(.055f,.012f,.004f,.94f));
          cw_node_set(d,"size",cw_val_vec3(spray&&!bubble?.13f:.28f+(i%3)*.13f,.32f,spray&&!bubble?.12f:.3f));
          cw_node3d_set_position(d,hit.point);
          t3_vec3 up=fabsf(hit.normal.z)>.9f?(t3_vec3){0,1,0}:(t3_vec3){0,0,1};
          cw_node3d_look_at(d,(t3_vec3){hit.point.x-hit.normal.x,hit.point.y-hit.normal.y,hit.point.z-hit.normal.z},up);
        }
      }
      o->position=next;
      float size=bubble?.13f:spray?.035f:.07f+(i%4)*.025f;o->scale=(t3_vec3){size,size*(spray?1.4f:1.6f-t*.5f),size};
    }
  }
}
/* Screen decals are instanced by the engine and kept until the scene is freed.
 * The simulation picks deposits for every rider, never a camera or render frame. */
static const track_desc *chocolate_track;
static float chocolate_carry[MAX_RIDERS], chocolate_last[MAX_RIDERS], chocolate_prev[MAX_RIDERS];
static int32_t chocolate_marks;
void view_chocolate_tick(int rider,float x,float y,float z,bool grounded,bool crashed) {
  if(!chocolate_track||strcmp(chocolate_track->theme,"candy"))return;
  float dx=x-chocolate_prev[rider];
  float moved=fabsf(dx);chocolate_prev[rider]=x;
  if(moved>3&&!crashed){chocolate_carry[rider]=0;chocolate_last[rider]=x;
    choc_splashed[rider]=false;choc_crash_coated[rider]=false;
    for(int i=0;i<24;i++)if(choc_drops[rider][i])choc_drops[rider][i]->visible=false;}
  float amount=chocolate_amount(chocolate_track,x);
  bool wet=amount>.05f&&fabsf(y-track_height_at(x,NULL))<(crashed?1.7f:.9f);
  if(grounded&&!wet)choc_crash_coated[rider]=false;
  bool crash_burst=crashed&&wet&&!choc_crash_coated[rider];
  if(crash_burst){models_chocolate(rider);choc_crash_coated[rider]=true;}
  if(wet&&(crash_burst||(grounded&&(!choc_splashed[rider]||choc_splash_age[rider]>2.2f)))) {
    choc_splash_power[rider]=crashed?1:.65f;
    choc_splash_vx[rider]=grounded?CW_CLAMP(dx*60,-30,30):0;
    choc_splashed[rider]=true;choc_splash_age[rider]=0;
    memset(choc_drop_done[rider],0,sizeof choc_drop_done[rider]);
    choc_splash_at[rider]=(t3_vec3){x,track_height_at(x,NULL)+.2f,z};choc_splash_count++;
  }
  if(!grounded)return;
  float ground=track_height_at(x,NULL);
  if(fabsf(y-ground)>.9f)return; /* elevated routes don't pick up the river below */
  if(amount>.1f)chocolate_carry[rider]=14;
  float distance=fabsf(x-chocolate_last[rider]);
  if(distance<.38f)return;
  chocolate_last[rider]=x;

  if(chocolate_carry[rider]<=0)return;
  chocolate_carry[rider]=fmaxf(0,chocolate_carry[rider]-fminf(distance,.6f));
  if(amount>.05f)return;
  cw_node d=cw_node_new("Decal3D","Chocolate tire print");
  cw_node_add_child(chocolate_parent,d);
  cw_node_set(d,"blend",cw_val_enum("MULTIPLY"));
  cw_node_set(d,"color",cw_val_color(.10f,.022f,.005f,.95f*chocolate_carry[rider]/14));
  cw_node_set(d,"size",cw_val_vec3(.56f,.27f,.35f));
  cw_node_set(d,"normal_fade",cw_val_float(0));
  cw_node3d_set_position(d,(t3_vec3){x,ground+.04f,z});
  float slope=atan2f(track_height_at(x+.1f,NULL)-track_height_at(x-.1f,NULL),.2f);
  cw_node3d_set_rotation(d,(t3_vec3){-1.5707963f,0,slope});
  chocolate_marks++;
}
