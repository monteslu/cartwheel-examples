/* Downtown Mesa facades from the user's reference photos. Logos come directly
 * from the venues' websites (see CREDITS). Each sign is a single small plane;
 * the architecture uses the same batched primitives as the other city props. */
#include "mesa-layout.h"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "cartwheel/platform.h"

static t3_material *mesa_sign_material(t3_texture *tx, bool alpha) {
  t3_material *m=t3_mesh_basic_material_new(0xffffff);
  tx->color_space=T3_SRGB_COLOR_SPACE;
  tx->generate_mipmaps=true;
  tx->min_filter=T3_LINEAR_MIPMAP_LINEAR;
  t3_material_set_map(m,tx);
  m->transparent=alpha;
  return m;
}
/* Temporary build cache: meshes retain materials after these references are
 * dropped. Never carries handles across scene unloads or renderer lifetimes. */
static struct { const char *file; t3_material *material; } mesa_sign_cache[64];
static int mesa_sign_cache_count;
static void mesa_sign_cache_clear(void) {
  for(int i=0;i<mesa_sign_cache_count;i++)t3_release(mesa_sign_cache[i].material);
  mesa_sign_cache_count=0;
}
static t3_material *mesa_logo_material(const char *file) {
  for(int i=0;i<mesa_sign_cache_count;i++)if(!strcmp(file,mesa_sign_cache[i].file)) {
    t3_retain(mesa_sign_cache[i].material);
    return mesa_sign_cache[i].material;
  }
  size_t size=0;
  void *bytes=cw_asset_load(file,&size);
  t3_texture *tx=bytes?t3_texture_load_memory(bytes,size):NULL;
  free(bytes);
  if(!tx) { cw_warn("bikemesa: missing landmark sign %s",file); return NULL; }
  if(!strcmp(file,"signs/margaritas-official.png") && tx->pixels) {
    for(int j=0;j<tx->height;j++)for(int i=0;i<tx->width;i++) {
      unsigned char *px=tx->pixels+(j*tx->width+i)*4;
      bool glass=i>tx->width*.60f&&i<tx->width*.72f&&j<tx->height*.78f;
      bool face=px[1]>px[0];
      px[0]=glass?136:face?223:68;px[1]=glass?192:face?65:76;px[2]=glass?66:face?61:38;
    }
    tx->version++;
  }
  bool espiritu=!strcmp(file,"signs/espiritu-wordmark.png");
  bool distilling=!strcmp(file,"signs/azdistilling.png");
  bool neon=!strcmp(file,"signs/chupacabra.png")||espiritu||distilling;
  /* Convert the monochrome print artwork into pink neon on transparent glass.
   * Done once at scene load; retain the original source logo unchanged. */
  if(neon && tx->pixels) {
    for(int i=0;i<tx->width*tx->height;i++) {
      unsigned char *px=tx->pixels+i*4;
      px[3]=(unsigned char)((255-px[0])*px[3]/255);
      px[0]=255;px[1]=espiritu?218:distilling?130:85;px[2]=espiritu?160:distilling?100:170;
    }
    tx->version++;
  }
  t3_material *m=mesa_sign_material(tx,true);
  if(neon)m->color=(t3_color){4,4,4};
  if(!strcmp(file,"signs/mangos-official.png"))m->color=(t3_color){1,.55f,.65f};
  if(!strcmp(file,"signs/pizzamart-reconstructed.png"))m->color=(t3_color){1.7f,1.7f,1.7f};
  t3_release(tx);
  if(mesa_sign_cache_count<(int)(sizeof mesa_sign_cache/sizeof mesa_sign_cache[0])) {
    mesa_sign_cache[mesa_sign_cache_count].file=file;
    mesa_sign_cache[mesa_sign_cache_count++].material=m;
    t3_retain(m);
  }
  return m;
}
static void mesa_logo(t3_object *root,const char *file,float x,float y,float z,float w,float h) {
  t3_material *m=mesa_logo_material(file);
  if(!m)return;
  t3_geometry *g=t3_plane_geometry_new(w,h,1,1);
  t3_mesh *sign=add(root,g,m,false);
  sign->base.position=(t3_vec3){x,y,z};
  t3_release(g);t3_release(m);
}
/* Bake a crisp nameplate once, using the game's shipped font. No per-frame
 * text shaping or external system font dependency, on native and carts alike. */
static void mesa_name(t3_object *root,const char *label,float x,float y,float z,float w,float h) {
  size_t size=0;
  bool hopes=!strcmp(label,"FRYBREAD")||!strcmp(label,"HOPE’S")||!strcmp(label,"HOPE'S");
  bool copal=!strcmp(label,"CASA")||!strcmp(label,"COPAL");
  bool coffee=!strcmp(label,"Lost Dutchman Coffee");
  bool architectural=coffee||copal||hopes||!strncmp(label,"DIAMOND",7)||!strncmp(label,"LEGENDARY",9)||!strcmp(label,"NCT")||!strcmp(label,"THE")||!strcmp(label,"NILE")||!strcmp(label,"EST. 1924")||!strcmp(label,"Concert");
  void *bytes=cw_asset_load(architectural?"fonts/Lato-Bold.ttf":"fonts/Fredoka-SemiBold.ttf",&size);
  if(!bytes) return;
  stbtt_fontinfo font;
  if(!stbtt_InitFont(&font,bytes,0)) {free(bytes);return;}
  enum {H=128}; int W=1024;
  float scale=stbtt_ScaleForPixelHeight(&font,94);
  int total=0;
  for(const char *c=label;*c;c++) {int a;stbtt_GetCodepointHMetrics(&font,*c,&a,NULL);total+=(int)(a*scale);}
  if(total>W-30) scale*=(float)(W-30)/total;
  total=0;
  for(const char *c=label;*c;c++) {int a;stbtt_GetCodepointHMetrics(&font,*c,&a,NULL);total+=(int)(a*scale);}
  W=total+30;
  unsigned char *rgba=malloc(W*H*4);
  bool chupa=!strcmp(label,"CHUPACABRA TAPROOM");
  bool neon=!strcmp(label,"OPEN")||!strncmp(label,"ARIZONA",7);
  bool nile=!strcmp(label,"THE")||!strcmp(label,"NILE")||!strcmp(label,"EST. 1924")||!strcmp(label,"Concert");
  bool chiwas=!strcmp(label,"Tacos Chiwas");
  bool burrito=!strcmp(label,"BURRITOHOLICS");
  bool diamond=!strncmp(label,"DIAMOND",7)||!strncmp(label,"LEGENDARY",9);
  bool letters=chupa||coffee||copal||hopes||diamond||!strcmp(label,"NCT")||burrito||chiwas||neon||nile||!strncmp(label,"MAIN",4)||!strcmp(label,"BURGERS")||!strncmp(label,"B.R.I.",6)||!strcmp(label,"OBC");
  bool distill=!strncmp(label,"ARIZONA",7);
  unsigned char ink[3]={20,20,20},paper[3]={245,235,213};
  if(letters) {ink[0]=!strncmp(label,"B.R.I.",6)?55:230;ink[1]=175;ink[2]=!strcmp(label,"OBC")?230:65;}
  if(coffee){ink[0]=74;ink[1]=45;ink[2]=34;}
  if(chupa){ink[0]=188;ink[1]=151;ink[2]=108;}
  if(copal){ink[0]=235;ink[1]=224;ink[2]=166;}
  if(hopes){ink[0]=35;ink[1]=190;ink[2]=210;}
  if(!strcmp(label,"HOPE'S")){ink[0]=220;ink[1]=240;ink[2]=235;}
  if(distill){paper[0]=24;paper[1]=30;paper[2]=43;ink[0]=245;ink[1]=130;ink[2]=100;}
  if(neon){ink[0]=255;ink[1]=85;ink[2]=170;}
  if(!strcmp(label,"NCT")){ink[0]=210;ink[1]=62;ink[2]=66;}
  if(burrito){ink[0]=225;ink[1]=221;ink[2]=184;}
  if(chiwas){ink[0]=40;ink[1]=95;ink[2]=64;}
  if(nile){ink[0]=245;ink[1]=238;ink[2]=216;}
  if(diamond){ink[0]=ink[1]=ink[2]=!strncmp(label,"DIAMOND",7)?235:22;}
  for(int i=0;i<W*H;i++) {for(int k=0;k<3;k++)rgba[i*4+k]=letters?ink[k]:paper[k];rgba[i*4+3]=letters?0:255;}
  int pen=(W-total)/2;
  for(const char *c=label;*c;c++) {
    int bw,bh,ox,oy,a;
    unsigned char *bm=stbtt_GetCodepointBitmap(&font,0,scale,*c,&bw,&bh,&ox,&oy);
    for(int yy=0;yy<bh;yy++) for(int xx=0;xx<bw;xx++) {
      int px=pen+ox+xx,py=96+oy+yy;
      if(px<0||px>=W||py<0||py>=H)continue;
      int at=(py*W+px)*4,alpha=bm[yy*bw+xx];
      if(letters)rgba[at+3]=(unsigned char)alpha;
      else for(int k=0;k<3;k++) rgba[at+k]=(unsigned char)((rgba[at+k]*(255-alpha)+ink[k]*alpha)/255);
    }
    stbtt_FreeBitmap(bm,NULL);
    stbtt_GetCodepointHMetrics(&font,*c,&a,NULL);pen+=(int)(a*scale);
  }
  free(bytes);
  t3_texture *tx=t3_texture_new(W,H,rgba);free(rgba);
  t3_material *m=mesa_sign_material(tx,letters);
  /* HDR pink feeds the existing bloom pass without adding a light or shader. */
  if(neon)m->color=(t3_color){4.0f,4.0f,4.0f};
  t3_geometry *g=t3_plane_geometry_new(w,h,1,1);
  t3_mesh *sign=add(root,g,m,false);sign->base.position=(t3_vec3){x,y,z};
  t3_release(g);t3_release(m);t3_release(tx);
}
static t3_material *mesa_brick(void) {
  enum {N=128}; unsigned char px[N*N*4];
  for(int y=0;y<N;y++)for(int x=0;x<N;x++) {
    bool mortar=y%16<2||(x+(y/16%2)*16)%32<2;
    unsigned h=sh((unsigned)(x/32+y/16*29));
    int at=(y*N+x)*4,v=(int)(h%24);
    px[at]=mortar?166:145+v;px[at+1]=mortar?145:65+v;px[at+2]=mortar?120:43+v;px[at+3]=255;
  }
  t3_texture *tx=t3_texture_new(N,N,px);
  tx->color_space=T3_SRGB_COLOR_SPACE;tx->wrap_s=tx->wrap_t=T3_REPEAT;
  tx->repeat=(t3_vec2){3,2};tx->generate_mipmaps=true;tx->min_filter=T3_LINEAR_MIPMAP_LINEAR;
  t3_material *m=t3_mesh_standard_material_new(0xffffff);
  m->roughness=0.85f;t3_material_set_map(m,tx);t3_release(tx);return m;
}
/* Brick dimensions stay consistent across wide walls and narrow pilasters. */
static void mesa_masonry(t3_object *root,t3_material *brick,float x,float y,float z,float w,float h,float d) {
  t3_geometry *g=t3_box_geometry_new(w,h,d,1,1,1);
  float *pos=g->attributes[T3_ATTR_POSITION]->array;
  float *normal=g->attributes[T3_ATTR_NORMAL]->array;
  float *uv=g->attributes[T3_ATTR_UV]->array;
  for(int i=0;i<g->attributes[T3_ATTR_POSITION]->count;i++) {
    float horizontal=fabsf(normal[i*3])>.5f?pos[i*3+2]:pos[i*3];
    float vertical=fabsf(normal[i*3+1])>.5f?pos[i*3+2]:pos[i*3+1];
    uv[i*2]=horizontal/(.88f*3);uv[i*2+1]=vertical/(.64f*2);
  }
  t3_mesh *m=add(root,g,brick,true);m->base.position=(t3_vec3){x,y,z};
  t3_release(g);
}
static void mesa_tall_buildings(t3_object *root,const whimsy_parts *p,t3_material *brick) {
  /* The Nile: red brick, nine upstairs sash windows, stepped pilasters and
   * a projecting theater marquee. These are all static batchable pieces. */
  float x=mesa_layout.nile,z=-14.5f,y=bank_y(x,z,2),w=16;
  t3_material *window=t3_mesh_standard_material_new(0x647c86);
  window->roughness=.65f;window->metalness=.05f;
  window->emissive=(t3_color){.025f,.035f,.045f};
  t3_material *storeglass=t3_mesh_standard_material_new(0x192c35);
  storeglass->roughness=.24f;storeglass->metalness=.25f;
  t3_material *curtain=t3_mesh_standard_material_new(0x9ca9ac);
  curtain->roughness=.92f;
  /* A shared shallow pleated surface suggests the photo's curtains without
   * transparent glass layers or individual cloth-strip boxes. */
  mb folds={0};
  for(int j=0;j<12;j++) {
    float xa=-.47f+j*(.94f/12),xb=xa+.94f/12;
    float za=(j%2)*.018f,zb=((j+1)%2)*.018f;
    t3_vec3 normal={za-zb,0,xb-xa};
    float inv=1/sqrtf(normal.x*normal.x+normal.z*normal.z);
    normal.x*=inv;normal.z*=inv;
    int v=mb_v(&folds,(t3_vec3){xa,-.975f,za},normal,0,0,1);
    mb_v(&folds,(t3_vec3){xb,-.975f,zb},normal,1,0,1);
    mb_v(&folds,(t3_vec3){xb,.975f,zb},normal,1,1,1);
    mb_v(&folds,(t3_vec3){xa,.975f,za},normal,0,1,1);
    mb_t(&folds,v,v+1,v+2);mb_t(&folds,v,v+2,v+3);
  }
  t3_geometry *curtain_geo=mb_done(&folds);
  mesa_masonry(root,brick,x,y+4.7f,z,w,9.4f,5);
  for(int i=0;i<4;i++) {
    float xx=x-w*.5f+i*w/3;
    mesa_masonry(root,brick,xx,y+4.95f,z+2.58f,.42f,9.9f,.25f);
    prop(root,p->box,p->cream,xx,y+9.93f,z+2.58f,.56f,.10f,.40f);
  }
  /* The reference has shallow steps between the four taller pilasters. */
  for(int bay=0;bay<3;bay++) {
    float bx=x-w*.5f+(bay+.5f)*w/3;
    prop(root,p->box,p->cream,bx,y+9.45f,z,w/3,.10f,5.15f);
    for(int side=-1;side<=1;side+=2) {
      float sx=bx+side*(w/6-.82f);
      mesa_masonry(root,brick,sx,y+9.51f,z+2.35f,1.64f,.22f,.55f);
      prop(root,p->box,p->cream,sx,y+9.66f,z+2.35f,1.64f,.08f,.66f);
    }
  }
  prop(root,p->box,p->cream,x,y+1.6f,z+2.55f,w-.45f,3.2f,.18f);
  for(int i=0;i<9;i++) {
    float xx=x-6.45f+(i/3)*5.25f+(i%3)*1.22f;
    prop(root,p->box,p->cream,xx,y+5.75f,z+2.68f,1.08f,2.15f,.16f);
    t3_mesh *drape=add(root,curtain_geo,curtain,false);
    drape->base.position=(t3_vec3){xx,y+5.75f,z+2.81f};
    prop(root,p->box,p->cream,xx,y+5.75f,z+2.83f,.045f,1.95f,.035f);
    prop(root,p->box,p->cream,xx,y+5.75f,z+2.83f,.94f,.045f,.035f);
    for(int pane=0;pane<4;pane++)
      prop(root,p->box,p->cream,xx,y+5.03f+pane*.48f,z+2.83f,.94f,.025f,.025f);
  }
  mesa_name(root,"THE",x,y+8.9f,z+2.76f,1.05f,.30f);
  mesa_name(root,"NILE",x,y+8.05f,z+2.76f,3.0f,2.15f);
  mesa_name(root,"EST. 1924",x,y+7.12f,z+2.76f,2.9f,.34f);
  for(int i=0;i<5;i++) {
    float xx=x-6.1f+i*3.05f;
    prop(root,p->box,p->black,xx,y+1.45f,z+2.69f,2.65f,2.8f,.08f);
    prop(root,p->box,storeglass,xx,y+1.65f,z+2.75f,2.42f,2.25f,.04f);
    prop(root,p->box,p->cream,xx,y+1.45f,z+2.80f,.06f,2.8f,.04f);
    prop(root,p->box,p->lamp,xx,y+2.7f,z+2.81f,2.2f,.035f,.035f);
  }
  /* The illuminated transom blocks and copper-edged marquee sit above
   * the entrances; the user's requested permanent wording is Concert. */
  prop(root,p->box,window,x,y+3.28f,z+2.75f,15.4f,.84f,.08f);
  /* Continuous glass-block transom, with thin mortar joints in both axes. */
  for(int i=0;i<=48;i++)
    prop(root,p->box,p->cream,x-7.7f+i*(15.4f/48),y+3.28f,z+2.80f,.022f,.84f,.025f);
  for(int i=0;i<=3;i++)
    prop(root,p->box,p->cream,x,y+2.86f+i*.28f,z+2.80f,15.4f,.022f,.025f);
  float mx=x+2.7f;
  prop(root,p->box,p->brown,mx,y+3.25f,z+3.35f,8.1f,1.45f,1.5f);
  prop(root,p->box,p->black,mx,y+3.25f,z+4.12f,7.75f,1.15f,.08f);
  mesa_name(root,"Concert",mx,y+3.25f,z+4.18f,4.6f,.92f);
  for(int side=-1;side<=1;side+=2) {
    for(int i=0;i<17;i++)
      prop(root,p->ball,p->lamp,mx-3.7f+i*.46f,y+3.25f+side*.64f,z+4.19f,.045f,.045f,.045f);
    prop(root,p->box,p->brown,mx+side*4,y+3.5f,z+3.5f,.34f,2,1.65f);
    prop(root,p->box,p->lamp,mx+side*4,y+3.5f,z+4.35f,.16f,1.65f,.025f);
  }
  /* Photo's Egyptian crest, not the venue's unrelated cursive web logo. */
  mesa_name(root,"THE",mx-1.48f,y+4.65f,z+3.96f,.65f,.6f);
  mesa_name(root,"NILE",mx+1.40f,y+4.65f,z+3.96f,.90f,.6f);
  /* Nemes headdress: peaked crown, broad shoulders and tapered lappets.
   * Two planar stripe meshes replace the old vertical bars around an oval. */
  mb headdress[2]={{0}};
  const float hood_width[]={.10f,.24f,.34f,.42f,.49f,.52f,.50f,.47f,.43f,.39f,.35f,.31f,.27f};
  for(int band=0;band<12;band++) {
    float top=y+5.28f-band*.10f,bot=top-.10f;
    mb *h=&headdress[band%2];
    int v=mb_v(h,(t3_vec3){mx-hood_width[band],top,z+3.98f},(t3_vec3){0,0,1},0,0,1);
    mb_v(h,(t3_vec3){mx-hood_width[band+1],bot,z+3.98f},(t3_vec3){0,0,1},0,1,1);
    mb_v(h,(t3_vec3){mx+hood_width[band+1],bot,z+3.98f},(t3_vec3){0,0,1},1,1,1);
    mb_v(h,(t3_vec3){mx+hood_width[band],top,z+3.98f},(t3_vec3){0,0,1},1,0,1);
    mb_t(h,v,v+1,v+2);mb_t(h,v,v+2,v+3);
  }
  for(int stripe=0;stripe<2;stripe++) {
    t3_geometry *hg=mb_done(&headdress[stripe]);
    add(root,hg,stripe?p->black:p->cream,false);t3_release(hg);
  }
  prop(root,p->ball,p->cream,mx,y+4.72f,z+4.03f,.20f,.34f,.05f);
  for(int side=-1;side<=1;side+=2)
    prop(root,p->box,p->black,mx+side*.09f,y+4.79f,z+4.085f,.075f,.025f,.018f);
  prop(root,p->box,p->cream,mx,y+4.70f,z+4.10f,.035f,.12f,.035f);
  prop(root,p->box,p->black,mx,y+4.58f,z+4.085f,.10f,.02f,.018f);
  /* The copper crest is pointed, with curved shoulders, as in the photo. */
  const float outline[][2]={{-2.5f,0},{-2.35f,.43f},{-1.95f,.82f},{-1.35f,1.1f},{-.55f,1.38f},{0,1.65f},{.55f,1.38f},{1.35f,1.1f},{1.95f,.82f},{2.35f,.43f},{2.5f,0}};
  mb crest={0};
  int center=mb_v(&crest,(t3_vec3){mx,y+4.1f,z+3.8f},(t3_vec3){0,0,1},0,0,1);
  for(int i=0;i<11;i++)mb_v(&crest,(t3_vec3){mx+outline[i][0],y+4.1f+outline[i][1],z+3.8f},(t3_vec3){0,0,1},0,0,1);
  for(int i=1;i<11;i++)mb_t(&crest,center,i+1,i);
  t3_geometry *cg=mb_done(&crest);add(root,cg,p->black,false);t3_release(cg);
  for(int i=0;i<10;i++) {
    float dx=outline[i+1][0]-outline[i][0],dy=outline[i+1][1]-outline[i][1];
    t3_mesh *trim=prop(root,p->box,p->brown,mx+(outline[i][0]+outline[i+1][0])*.5f,y+4.1f+(outline[i][1]+outline[i+1][1])*.5f,z+3.91f,sqrtf(dx*dx+dy*dy),.11f,.12f);
    t3_object_set_rotation(trim,0,0,atan2f(dy,dx));
  }
  for(int i=0;i<4;i++) {
    float xx=x-6+i*4;
    prop(root,p->box,p->black,xx,y+7.05f,z+2.95f,.08f,.10f,.7f);
    prop(root,p->cone,p->black,xx,y+6.96f,z+3.25f,.24f,.16f,.24f);
    prop(root,p->ball,p->lamp,xx,y+6.87f,z+3.25f,.16f,.035f,.16f);
  }

  t3_release(curtain_geo);t3_release(curtain);t3_release(storeglass);
  t3_release(window);
  /* Level One: tall white frontage, paired upstairs windows and suspended
   * steel sunshades, with a recessed arcade/patio behind the railing. */
  int level_first=root->child_count;
  x=mesa_layout.level_one;z=-22.5f;y=bank_y(x,z,2);w=12;
  t3_material *white=t3_mesh_standard_material_new(0xe3e5e2);
  white->roughness=.88f;
  t3_material *levelglass=t3_mesh_standard_material_new(0x526773);
  levelglass->roughness=.5f;
  prop(root,p->box,white,x,y+5,z,w,10,5);
  prop(root,p->box,p->cream,x,y+10.07f,z,w+.16f,.15f,5.12f);
  for(int side=-1;side<=1;side+=2) {
    float xx=x+side*2.65f;
    /* Pale recessed surrounds and projecting lintels in the reference. */
    prop(root,p->box,p->cream,xx,y+6.15f,z+2.53f,2.15f,2.72f,.12f);
    prop(root,p->box,p->black,xx,y+6.15f,z+2.55f,1.95f,2.5f,.1f);
    prop(root,p->box,levelglass,xx,y+6.15f,z+2.62f,1.7f,2.27f,.04f);
    prop(root,p->box,p->cream,xx,y+6.15f,z+2.66f,1.72f,.07f,.04f);
    prop(root,p->box,p->cream,xx,y+6.15f,z+2.66f,.055f,2.28f,.04f);
    prop(root,p->box,p->black,xx,y+7.55f,z+3,2.55f,.12f,1.05f);
    prop(root,p->box,p->cream,xx,y+7.45f,z+2.7f,2.12f,.16f,.36f);
    for(int j=-1;j<=1;j+=2) {
      t3_mesh *rod=prop(root,p->box,p->black,xx+j,y+7.98f,z+2.88f,.045f,1.05f,.045f);
      t3_object_set_rotation(rod,-.55f,0,0);
    }
    prop(root,p->cyl,p->black,x+side*5.65f,y+5.1f,z+2.63f,.07f,9.5f,.07f);
    t3_mesh *cap=prop(root,p->cyl,p->black,x+side*5.65f,y+9.55f,z+2.78f,.3f,.22f,.3f);
    t3_object_set_rotation(cap,1.5707963f,0,0);
  }
  mesa_logo(root,"signs/levelone.jpg",x,y+8.77f,z+2.67f,4.5f,2.25f);
  prop(root,p->box,p->black,x,y+1.65f,z+2.56f,10.4f,3.3f,.10f);
  prop(root,p->box,white,x,y+.25f,z+3.8f,10.4f,.24f,2.6f);
  /* Paved forecourt connects the taller, recessed facade to the sidewalk. */
  prop(root,p->box,white,x,y+.04f,z+8.5f,10.4f,.08f,7.0f);
  prop(root,p->box,p->black,x,y+3.65f,z+3.35f,12.3f,.15f,1.9f);
  for(int i=0;i<5;i++) {
    float xx=x-4+i*2;
    t3_mesh *rod=prop(root,p->box,p->black,xx,y+4.25f,z+3.0f,.05f,1.4f,.05f);
    t3_object_set_rotation(rod,-.7f,0,0);
    /* Patio stools in the recess, matching the photograph's open terrace. */
    prop(root,p->box,p->black,xx,y+1.0f,z+3.35f,.65f,.08f,.65f);
    prop(root,p->box,p->red,xx,y+1.32f,z+3.05f,.65f,.6f,.06f);
    for(int side=-1;side<=1;side+=2)
      prop(root,p->box,p->black,xx+side*.25f,y+.65f,z+3.35f,.045f,.7f,.5f);
  }
  for(int i=0;i<9;i++)prop(root,p->box,p->black,x-4.9f+i*1.225f,y+.95f,z+5.1f,.055f,1.4f,.055f);
  for(int i=0;i<3;i++)prop(root,p->box,p->black,x,y+.55f+i*.4f,z+5.1f,10,.045f,.045f);
  for(int i=level_first;i<root->child_count;i++) {
    root->children[i]->position.x=x+(root->children[i]->position.x-x)*.83f;
    root->children[i]->scale.x*=.83f;
    root->children[i]->position.y=y+(root->children[i]->position.y-y)*1.15f;
    root->children[i]->scale.y*=1.15f;
  }
  t3_release(levelglass);t3_release(white);
}
static void mesa_neighbor_restaurants(t3_object *root,const whimsy_parts *p) {
  t3_material *white=mat(TEX_CONCRETE,(float[3]){.9f,.88f,.82f},1,1,.1f,.9f);
  t3_material *charcoal=mat(TEX_CONCRETE,(float[3]){.23f,.24f,.23f},1,1,.1f,.9f);
  t3_material *orange=toy_paint(.8f,.25f,.10f);
  float z=-14.5f,y=bank_y(mesa_layout.worth_burritoholics_center,z,2);
  /* One continuous canopy; distinct adjacent restaurants and patio spaces. */
  for(int venue=0;venue<2;venue++) {
    float x=mesa_layout.worth_burritoholics_center-5+venue*10;
    prop(root,p->box,venue?charcoal:white,x,y+3.1f,z,10,6.2f,5);
    prop(root,p->box,venue?charcoal:white,x,y+6.26f,z,10,.15f,5.2f);
    prop(root,p->box,p->black,x,y+1.55f,z+2.57f,9.7f,3.1f,.1f);
    for(int i=0;i<5;i++) {
      float xx=x-4+i*2;
      prop(root,p->box,p->glass,xx,y+1.55f,z+2.66f,1.85f,2.8f,.04f);
      prop(root,p->box,p->cream,xx-.95f,y+1.55f,z+2.7f,.055f,3,.04f);
      prop(root,p->box,p->black,xx,y+3.35f,z+3.3f,2,.14f,1.6f);
      t3_mesh *rod=prop(root,p->box,p->black,xx,y+4,z+2.95f,.035f,1.5f,.035f);
      t3_object_set_rotation(rod,-.6f,0,0);
      prop(root,p->ball,p->lamp,xx,y+3.16f,z+4.03f,.035f,.06f,.035f);
    }
    if(venue)mesa_name(root,"BURRITOHOLICS",x,y+5.15f,z+2.7f,8.8f,.9f);
    else {
      mesa_logo(root,"signs/worth.jpg",x-3.3f,y+5.15f,z+2.72f,2.4f,2.4f);
      mesa_name(root,"WORTH",x+.8f,y+5.35f,z+2.7f,5.4f,.85f);
      mesa_name(root,"TAKEAWAY",x+.8f,y+4.75f,z+2.7f,3.5f,.38f);
    }
    for(int i=0;i<3;i++) {
      float xx=x-3.15f+i*3.15f;
      prop(root,p->cyl,p->brown,xx,y+1.35f,z+4.5f,.045f,2.7f,.045f);
      prop(root,p->cone,venue?p->cream:orange,xx,y+2.8f,z+4.5f,1.65f,.43f,1.2f);
      prop(root,p->cyl,p->black,xx,y+.55f,z+4.5f,.09f,1.1f,.09f);
      prop(root,p->cyl,p->brown,xx,y+1.05f,z+4.5f,.65f,.08f,.65f);
      for(int side=-1;side<=1;side+=2) {
        prop(root,p->box,p->black,xx+side*.9f,y+.55f,z+4.5f,.55f,.08f,.6f);
        prop(root,p->box,p->black,xx+side*1.1f,y+.85f,z+4.5f,.06f,.65f,.6f);
      }
    }
    if(venue) {
      for(int i=0;i<9;i++)prop(root,p->box,p->black,x-4.6f+i*1.15f,y+.65f,z+5.9f,.055f,1.3f,.055f);
      for(int i=0;i<4;i++)prop(root,p->box,p->black,x,y+.35f+i*.28f,z+5.9f,9.3f,.045f,.045f);
    } else for(int i=0;i<4;i++) {
      float xx=x-3.6f+i*2.4f;
      prop(root,p->box,charcoal,xx,y+.5f,z+5.9f,2.15f,1,.65f);
      prop(root,p->ball,p->green,xx,y+1.03f,z+5.9f,.9f,.22f,.32f);
    }
  }
  t3_release(white);t3_release(charcoal);t3_release(orange);
}
static void mesa_food_shops(t3_object *root,const whimsy_parts *p,t3_material *brick) {
  t3_material *stone=mat(TEX_CONCRETE,(float[3]){.66f,.65f,.60f},1,1,.1f,.9f);
  const float positions[]={mesa_layout.tacos_chiwas,mesa_layout.proof_bread,mesa_layout.mad_candy};
  for(int venue=0;venue<3;venue++) {
    float x=positions[venue],z=-14.5f,y=bank_y(x,z,2),w=venue==1?11:13;
    prop(root,p->box,venue==2?brick:stone,x,y+3,z,w,6,5);
    prop(root,p->box,p->cream,x,y+6.08f,z,w+.25f,.25f,5.2f);
    prop(root,p->box,p->cream,x,y+5.75f,z+2.6f,w,.25f,.25f);
    for(int i=0;i<22;i++)prop(root,p->box,p->cream,x-w/2+.3f+i*(w-.6f)/21,y+5.95f,z+2.7f,.16f,.3f,.18f);
    for(int side=-1;side<=1;side+=2)
      prop(root,p->box,p->cream,x+side*(w/2-.25f),y+2.8f,z+2.6f,.5f,5.5f,.3f);
    for(int i=0;i<4;i++) {
      float xx=x-w*.36f+i*w*.24f;
      prop(root,p->box,p->cream,xx,y+1.65f,z+2.6f,w*.23f,2.9f,.12f);
      prop(root,p->box,p->glass,xx,y+1.65f,z+2.69f,w*.23f-.12f,2.72f,.05f);
      prop(root,p->box,p->cream,xx,y+1.75f,z+2.74f,w*.23f,.055f,.04f);
    }
    if(venue==0) {
      mesa_name(root,"Tacos Chiwas",x,y+4.3f,z+2.8f,9.2f,1.1f);
      /* Open pergola, shaded patio and restrained warm festoon bulbs. */
      for(int side=-1;side<=1;side+=2)
        prop(root,p->cyl,p->cream,x+side*5.8f,y+2.1f,z+5,.10f,4.2f,.10f);
      prop(root,p->box,p->cream,x,y+4.85f,z+4,12.2f,.14f,2.6f);
      for(int i=0;i<13;i++) {
        float xx=x-5.6f+i*.93f;
        prop(root,p->box,p->black,xx,y+4.95f,z+4,.08f,.05f,2.7f);
        prop(root,p->ball,p->lamp,xx,y+4.55f-.15f*cw_sin(i*.26f),z+5.2f,.05f,.07f,.05f);
      }
      prop(root,p->box,p->brown,x,y+.65f,z+5.2f,11.6f,1.3f,.16f);
      for(int side=-1;side<=1;side+=2) {
        prop(root,p->cone,p->brown,x+side*5.2f,y+3.5f,z+4.7f,.34f,.45f,.34f);
        prop(root,p->ball,p->green,x+side*5.2f,y+3.65f,z+4.7f,.5f,.4f,.5f);
      }
    } else if(venue==1) {
      mesa_logo(root,"signs/proof.png",x,y+4.65f,z+2.8f,1.65f,2.55f);
      prop(root,p->box,p->black,x,y+3.2f,z+3.05f,w-.4f,.2f,1.1f);
      for(int i=0;i<19;i++)prop(root,p->ball,p->lamp,x-4.7f+i*.52f,y+3.08f,z+3.62f,.035f,.05f,.035f);
      for(int side=-1;side<=1;side+=2) {
        prop(root,p->box,p->cream,x+side*.65f,y+1.45f,z+2.82f,1.25f,2.75f,.08f);
        prop(root,p->box,p->glass,x+side*.65f,y+1.55f,z+2.88f,1.04f,2.3f,.03f);
        prop(root,p->cyl,p->black,x+side*3.4f,y+.6f,z+4,.12f,1.2f,.12f);
        prop(root,p->cyl,p->brown,x+side*3.4f,y+1.25f,z+4,.75f,.09f,.75f);
      }
    } else {
      /* Scalloped arcade: shallow arch segments share a single geometry. */
      prop(root,p->box,p->cream,x,y+4.8f,z+3.6f,w,2.1f,.4f);
      t3_geometry *arch=t3_torus_geometry_new(2.9f,.18f,6,20,3.1415927f);
      for(int side=-1;side<=1;side+=2) {
        t3_mesh *a=add(root,arch,p->cream,true);
        a->base.position=(t3_vec3){x+side*3.1f,y+3.3f,z+3.7f};a->base.scale.y=.35f;
      }
      t3_release(arch);
      for(int i=-1;i<=1;i++)prop(root,p->box,p->cream,x+i*6.2f,y+1.85f,z+3.6f,.55f,3.7f,.55f);
      mesa_logo(root,"signs/madcandy.png",x,y+4.8f,z+3.83f,3.5f,2.65f);
      prop(root,p->box,p->pink,x-3.1f,y+1.45f,z+2.84f,1.55f,2.85f,.12f);
      for(int i=0;i<3;i++) {
        float xx=x+1.3f+i*1.35f;
        prop(root,p->box,p->cream,xx,y+1.1f,z+2.86f,.045f,1.5f,.045f);
        prop(root,p->ball,i%2?p->pink:p->blue,xx,y+1.95f,z+2.91f,.5f,.5f,.09f);
        prop(root,p->ring,p->cream,xx,y+1.95f,z+3.03f,.32f,.32f,.32f);
      }
    }
  }
  t3_release(stone);
}
/* Fabric awnings: shared primitives merge with other static facade batches. */
static void mesa_striped_awning(t3_object *root,const whimsy_parts *p,
                                float x,float y,float z,float width) {
  int stripes=(int)(width/.28f);float sw=width/stripes;
  mb cloth[2]={{0},{0}};
  float rise=1.05f*cw_sin(.42f),reach=1.05f*cw_cos(.42f);
  for(int i=0;i<stripes;i++) {
    mb *m=&cloth[i%2];float left=x-width*.5f+i*sw,right=left+sw;
    t3_vec3 n={0,cw_cos(.42f),cw_sin(.42f)};
    int k=mb_v(m,(t3_vec3){left,y+rise,z-reach},n,0,0,1);
    mb_v(m,(t3_vec3){right,y+rise,z-reach},n,1,0,1);
    mb_v(m,(t3_vec3){right,y-rise,z+reach},n,1,1,1);
    mb_v(m,(t3_vec3){left,y-rise,z+reach},n,0,1,1);
    mb_t(m,k,k+2,k+1);mb_t(m,k,k+3,k+2);
    /* Explicit reverse faces retain the existing opaque material batches. */
    for(int j=0;j<4;j++) {
      t3_vec3 v=j==0?(t3_vec3){left,y+rise,z-reach}:j==1?(t3_vec3){right,y+rise,z-reach}:j==2?(t3_vec3){right,y-rise,z+reach}:(t3_vec3){left,y-rise,z+reach};
      mb_v(m,v,(t3_vec3){-n.x,-n.y,-n.z},0,0,1);
    }
    mb_t(m,k+4,k+5,k+6);mb_t(m,k+4,k+6,k+7);
    for(int j=0;j<4;j++)for(int side=0;side<2;side++) {
      float u=j/4.f,v=(j+1)/4.f;
      float ya=y-rise-.10f-.13f*sinf(u*3.1415927f);
      float yb=y-rise-.10f-.13f*sinf(v*3.1415927f);
      n=(t3_vec3){0,0,side?-1:1};
      k=mb_v(m,(t3_vec3){left+sw*u,y-rise,z+reach},n,0,0,1);
      mb_v(m,(t3_vec3){left+sw*u,ya,z+reach},n,0,1,1);
      mb_v(m,(t3_vec3){left+sw*v,yb,z+reach},n,1,1,1);
      mb_v(m,(t3_vec3){left+sw*v,y-rise,z+reach},n,1,0,1);
      if(side){mb_t(m,k,k+2,k+1);mb_t(m,k,k+3,k+2);}
      else{mb_t(m,k,k+1,k+2);mb_t(m,k,k+2,k+3);}
    }
  }
  for(int i=0;i<2;i++) {
    t3_geometry *g=mb_done(&cloth[i]);add(root,g,i?p->cream:p->red,true);t3_release(g);
  }
  for(int side=-1;side<=1;side+=2) {
    t3_mesh *o=prop(root,p->box,p->black,x+side*(width*.5f-.12f),y-.22f,z,.035f,.035f,2.12f);
    t3_object_set_rotation(o,.42f,0,0);
  }
}
/* Raised NCT channel letters: opaque geometry batches retain lighting and
 * cast real shadows. The inset salmon face leaves a dark cabinet rim. */
static void mesa_letter_quad(mb *m,const t3_vec2 v[4],float depth) {
  int k=mb_v(m,(t3_vec3){v[0].x,v[0].y,depth},(t3_vec3){0,0,1},0,0,1);
  for(int i=1;i<4;i++)mb_v(m,(t3_vec3){v[i].x,v[i].y,depth},(t3_vec3){0,0,1},0,0,1);
  mb_t(m,k,k+1,k+2);mb_t(m,k,k+2,k+3);
  for(int i=0;i<4;i++) {
    t3_vec2 a=v[i],b=v[(i+1)%4];t3_vec3 n={b.y-a.y,a.x-b.x,0};
    k=mb_v(m,(t3_vec3){a.x,a.y,0},n,0,0,1);
    mb_v(m,(t3_vec3){b.x,b.y,0},n,0,0,1);
    mb_v(m,(t3_vec3){b.x,b.y,depth},n,0,0,1);
    mb_v(m,(t3_vec3){a.x,a.y,depth},n,0,0,1);
    mb_t(m,k,k+1,k+2);mb_t(m,k,k+2,k+3);
  }
}
static void mesa_letter_bar(mb *m,float x0,float y0,float x1,float y1,float depth) {
  t3_vec2 v[4]={{x0,y0},{x1,y0},{x1,y1},{x0,y1}};
  mesa_letter_quad(m,v,depth);
}
static void mesa_nct_letters(t3_object *root,float x,float y,float z) {
  for(int layer=0;layer<2;layer++) {
    mb m={0};float inset=layer?.045f:0,depth=layer?.025f:.18f;
    float bottom=-.7f+inset,top=.7f-inset,stroke=.31f-2*inset;
    /* N */
    mesa_letter_bar(&m,-1.95f+inset,bottom,-1.95f+inset+stroke,top,depth);
    mesa_letter_bar(&m,-.8f-inset-stroke,bottom,-.8f-inset,top,depth);
    t3_vec2 diagonal[4]={{-1.95f+inset,top},{-1.95f+inset+stroke,top},
                           {-.8f-inset,bottom},{-.8f-inset-stroke,bottom}};
    /* Reverse the clockwise diagonal for outward front normals. */
    t3_vec2 ordered[4]={diagonal[3],diagonal[2],diagonal[1],diagonal[0]};
    mesa_letter_quad(&m,ordered,depth);
    /* C: round channel with an open right-hand mouth. */
    float outer=.7f-inset,inner=.39f+inset;
    for(int i=0;i<24;i++) {
      float a=.55f+i*(6.2831853f-1.1f)/24,b=a+(6.2831853f-1.1f)/24;
      t3_vec2 v[4]={{outer*cw_cos(a),outer*cw_sin(a)},
        {outer*cw_cos(b),outer*cw_sin(b)},
        {inner*cw_cos(b),inner*cw_sin(b)},
        {inner*cw_cos(a),inner*cw_sin(a)}};
      mesa_letter_quad(&m,v,depth);
    }
    /* T */
    mesa_letter_bar(&m,.85f+inset,top-stroke,1.95f-inset,top,depth);
    mesa_letter_bar(&m,1.4f-stroke*.5f,bottom,1.4f+stroke*.5f,top,depth);
    t3_geometry *g=mb_done(&m);
    t3_material *paint=t3_mesh_standard_material_new(layer?0xd7646c:0x713a43);
    paint->roughness=.54f;paint->metalness=.18f;
    t3_mesh *o=add(root,g,paint,true);o->base.position=(t3_vec3){x,y,z+(layer?.182f:0)};
    t3_release(paint);t3_release(g);
  }
}
static void mesa_nct(t3_object *root,const whimsy_parts *p,t3_material *brick) {
  float x=mesa_layout.nct,z=-14.5f,y=bank_y(x,z,2);
  /* The upper apartments in the photograph belong to the background. */
  mesa_masonry(root,brick,x,y+3.4f,z,13,6.8f,5);
  mesa_masonry(root,brick,x+4.15f,y+7.35f,z,4.7f,1.1f,5);
  mesa_masonry(root,brick,x-2.35f,y+6.85f,z+2.57f,8.3f,.2f,.2f);
  mesa_masonry(root,brick,x+4.15f,y+7.95f,z+2.57f,4.7f,.2f,.2f);
  t3_material *glass=t3_mesh_standard_material_new(0x15222a);
  glass->roughness=.22f;glass->metalness=.35f;
  prop(root,p->box,glass,x-2.15f,y+2.2f,z+2.56f,8.1f,3.75f,.08f);
  prop(root,p->box,p->cream,x-2.15f,y+.27f,z+2.62f,8.15f,.38f,.11f);
  for(int i=0;i<5;i++)prop(root,p->box,p->cream,x-6.2f+i*2.025f,y+2.18f,z+2.64f,.07f,3.9f,.08f);
  prop(root,p->box,p->cream,x-2.15f,y+4.12f,z+2.64f,8.2f,.09f,.1f);
  prop(root,p->box,glass,x+4.25f,y+2.05f,z+2.57f,2.7f,4.1f,.08f);
  for(int i=0;i<3;i++)prop(root,p->box,p->cream,x+2.9f+i*1.35f,y+2.05f,z+2.65f,.10f,4.1f,.1f);
  for(int i=0;i<3;i++)prop(root,p->box,p->cream,x+4.25f,y+.1f+i*2,z+2.67f,2.8f,.10f,.1f);
  prop(root,p->box,p->cream,x+5.35f,y+1.3f,z+2.76f,.06f,.55f,.08f);
  mesa_striped_awning(root,p,x-2.1f,y+4.55f,z+3.5f,8.6f);
  t3_mesh *canopy=prop(root,p->box,p->red,x+4.2f,y+4.35f,z+3.05f,4.7f,.08f,1.1f);
  t3_object_set_rotation(canopy,.25f,0,0);
  mesa_nct_letters(root,x+4.1f,y+5.65f,z+2.68f);
  prop(root,p->box,p->black,x+4.1f,y+7.05f,z+2.65f,1.5f,1.5f,.16f);
  mesa_logo(root,"signs/nct-reconstructed.png",x+4.1f,y+7.05f,z+2.75f,1.44f,1.44f);
  mesa_logo(root,"signs/sacred-pint-reconstructed.png",x-2.2f,y+2.05f,z+2.72f,2.15f,2.4f);
  for(int i=0;i<5;i++) {
    float xx=x-5+i*2.6f,ly=y+(i>=3?7.6f:6.12f);
    prop(root,p->ball,p->black,xx,ly,z+2.63f,.1f,.1f,.1f);
    t3_mesh *rod=prop(root,p->box,p->black,xx,ly-.14f,z+2.8f,.035f,.035f,.5f);
    t3_object_set_rotation(rod,.65f,0,0);
  }
  t3_release(glass);
}
/* Eight fabric panels, two merged material batches per canopy. The skirt
 * follows the octagonal edge; no textures, transparency or extra lights. */
static void mesa_patio_umbrella(t3_object *root,const whimsy_parts *p,
                                t3_material *burgundy,float x,float y,float z) {
  mb panels[2]={{0},{0}};
  for(int i=0;i<8;i++) {
    float a=i*.785398163f+.392699082f,b=a+.785398163f;
    t3_vec3 left={1.65f*cw_cos(a),0,1.65f*cw_sin(a)};
    t3_vec3 right={1.65f*cw_cos(b),0,1.65f*cw_sin(b)};
    t3_vec3 n={.25f*cw_cos((a+b)*.5f),.96f,.25f*cw_sin((a+b)*.5f)};
    mb *mesh=&panels[i%2];
    int k=mb_v(mesh,(t3_vec3){0,.48f,0},n,.5f,0,1);
    mb_v(mesh,right,n,1,1,1);mb_v(mesh,left,n,0,1,1);
    mb_t(mesh,k,k+1,k+2);
    n=(t3_vec3){cw_cos((a+b)*.5f),0,cw_sin((a+b)*.5f)};
    k=mb_v(mesh,left,n,0,0,1);mb_v(mesh,right,n,1,0,1);
    right.y=-.19f;left.y=-.19f;
    mb_v(mesh,right,n,1,1,1);mb_v(mesh,left,n,0,1,1);
    mb_t(mesh,k,k+1,k+2);mb_t(mesh,k,k+2,k+3);
  }
  for(int i=0;i<2;i++) {
    t3_geometry *g=mb_done(&panels[i]);
    prop(root,g,i?p->cream:burgundy,x,y,z,1,1,1);t3_release(g);
  }
  prop(root,p->ball,burgundy,x,y+.49f,z,.12f,.045f,.12f);
}
static void mesa_gus(t3_object *root,const whimsy_parts *p,t3_material *brick) {
  float x=mesa_layout.gus,z=-14.5f,y=bank_y(x,z,2);
  t3_material *white=t3_mesh_standard_material_new(0xf0e9dd);
  white->roughness=.88f;
  prop(root,p->box,white,x,y+3.25f,z,15,6.5f,5);
  prop(root,p->box,white,x-1.5f,y+7.1f,z,6.2f,1.2f,5);
  mesa_masonry(root,brick,x-1.5f,y+7.76f,z+2.55f,6.55f,.3f,.22f);
  for(int side=-1;side<=1;side+=2) {
    float xx=x-1.5f+side*3.2f;
    mesa_masonry(root,brick,xx,y+7.1f,z+2.55f,.22f,1.2f,.22f);
  }
  mesa_masonry(root,brick,x-6.05f,y+6.56f,z+2.55f,2.9f,.3f,.22f);
  mesa_masonry(root,brick,x+4.6f,y+6.56f,z+2.55f,5.8f,.3f,.22f);
  t3_material *glass=t3_mesh_standard_material_new(0x192629);
  glass->roughness=.2f;glass->metalness=.3f;
  prop(root,p->box,glass,x+1.45f,y+2.05f,z+2.57f,10.8f,3.8f,.08f);
  for(int i=0;i<5;i++)prop(root,p->box,p->cream,x-3.95f+i*2.7f,y+2.05f,z+2.65f,.09f,3.9f,.08f);
  for(int i=0;i<2;i++)prop(root,p->box,p->cream,x+1.45f,y+.15f+i*3.8f,z+2.65f,10.9f,.1f,.08f);
  prop(root,p->box,p->gold,x-5.6f,y+1.8f,z+2.62f,1.8f,3.6f,.13f);
  prop(root,p->box,glass,x-5.6f,y+1.95f,z+2.7f,1.5f,2.95f,.04f);
  prop(root,p->box,p->cream,x-4.95f,y+1.2f,z+2.79f,.045f,.55f,.06f);
  for(int i=0;i<3;i++)mesa_masonry(root,brick,x-7.3f+i*1.7f,y+2.05f,z+2.6f,.25f,4.1f,.32f);
  mesa_striped_awning(root,p,x+1.45f,y+4.65f,z+3.6f,11.8f);
  t3_mesh *hood=prop(root,p->box,p->red,x-5.7f,y+4.25f,z+3.1f,3.5f,.09f,1.2f);
  t3_object_set_rotation(hood,.3f,0,0);
  /* Projecting cabinet and textured neon tubes. Only pale tube pixels emit;
   * the yellow painted backing remains below the bloom threshold. */
  size_t size=0;void *bytes=cw_asset_load("signs/gus-reconstructed.png",&size);
  t3_texture *tx=bytes?t3_texture_load_memory(bytes,size):NULL;free(bytes);
  if(tx) {
    tx->color_space=T3_SRGB_COLOR_SPACE;tx->generate_mipmaps=true;tx->min_filter=T3_LINEAR_MIPMAP_LINEAR;
    t3_material *m=mesa_sign_material(tx,false);
    /* Separate additive tube layer reuses the existing transparent basic shader. */
    unsigned char *glow=calloc((size_t)tx->width*tx->height,4);
    for(int i=0;i<tx->width*tx->height;i++) {
      const unsigned char *p0=tx->pixels+i*4;unsigned char *q=glow+i*4;
      int lo=p0[0]<p0[1]?p0[0]:p0[1];if(p0[2]<lo)lo=p0[2];
      int hi=p0[0]>p0[1]?p0[0]:p0[1];if(p0[2]>hi)hi=p0[2];
      q[3]=(lo>125 && hi-lo<85)?(unsigned char)((lo-125)*255/130):0;
      bool green=i/(tx->width)>(int)(tx->height*.82f);
      q[0]=green?65:255;q[1]=green?255:110;q[2]=80;
    }
    t3_texture *gt=t3_texture_new(tx->width,tx->height,glow);free(glow);
    t3_material *gm=mesa_sign_material(gt,true);gm->color=(t3_color){4,4,4};
    int first=root->child_count;
    prop(root,p->box,p->red,0,0,0,2.15f,3.25f,.32f);
    t3_geometry *plane=t3_plane_geometry_new(2.02f,3.12f,1,1);
    t3_mesh *face=add(root,plane,m,false);face->base.position.z=.17f;
    t3_mesh *tubes=add(root,plane,gm,false);tubes->base.position.z=.175f;
    float angle=.65f,cs=cw_cos(angle),sn=cw_sin(angle);
    for(int j=first;j<root->child_count;j++) {
      t3_object *o=root->children[j];float px=o->position.x,pz=o->position.z;
      o->position=(t3_vec3){x-1.3f+px*cs+pz*sn,y+6.15f+o->position.y,z+3.6f-px*sn+pz*cs};
      t3_object_set_rotation(o,0,angle,0);
    }
    for(int i=0;i<2;i++)prop(root,p->box,p->black,x-2.1f,y+5.05f+i*2.1f,z+2.95f,.15f,.16f,.9f);
    t3_release(plane);t3_release(m);t3_release(gm);t3_release(gt);t3_release(tx);
  }
  t3_material *burgundy=t3_mesh_standard_material_new(0x713536);
  burgundy->roughness=.94f;
  for(int i=0;i<2;i++) {
    float xx=x+.2f+i*4.2f,zz=z+3.65f;
    prop(root,p->box,p->brown,xx,y+1.05f,zz,1.6f,.12f,1.3f);
    for(int side=-1;side<=1;side+=2) {
      prop(root,p->box,p->brown,xx+side*.63f,y+.5f,zz,.09f,1,.9f);
      prop(root,p->box,p->brown,xx+side*1.12f,y+.6f,zz,.65f,.09f,.7f);
      for(int leg=-1;leg<=1;leg+=2) {
        prop(root,p->box,p->brown,xx+side*1.4f,y+.67f,zz+leg*.28f,.065f,1.3f,.065f);
        prop(root,p->box,p->brown,xx+side*.86f,y+.3f,zz+leg*.28f,.065f,.6f,.065f);
      }
      for(int slat=0;slat<2;slat++)
        prop(root,p->box,p->brown,xx+side*1.4f,y+.98f+slat*.22f,zz,.075f,.16f,.65f);
    }
    prop(root,p->cyl,p->cream,xx,y+1.8f,zz,.035f,3.4f,.035f);
    mesa_patio_umbrella(root,p,burgundy,xx,y+2.95f,zz);
  }
  t3_release(burgundy);t3_release(glass);t3_release(white);
}
/* Diamond Don's: two shallow arcade arches in a white stepped facade.
 * The official guitarist emblem is separate from the readable nameplate. */
static void mesa_diamond_dons(t3_object *root,const whimsy_parts *p) {
  float x=mesa_layout.diamond_dons,z=-14.5f,y=bank_y(x,z,2);
  t3_material *white=mat(TEX_CONCRETE,(float[3]){.85f,.84f,.79f},1,1,.05f,.86f);
  t3_material *glass=toy_paint(.055f,.075f,.08f);glass->roughness=.3f;glass->metalness=.25f;
  prop(root,p->box,white,x,y+2.8f,z-1,15.2f,5.6f,3);
  for(int side=-1;side<=1;side+=2) {
    prop(root,p->box,white,x+side*8.15f,y+2.15f,z-1,1.1f,4.3f,3);
    prop(root,p->box,white,x+side*8.15f,y+3.9f,z+2.6f,1.1f,.8f,.65f);
    prop(root,p->box,white,x+side*8.15f,y+4.34f,z+2.6f,1.16f,.09f,.8f);
    prop(root,p->box,white,x+side*8.67f,y+1.75f,z+2.6f,.16f,3.5f,.65f);
  }
  /* Recessed porch wall and floor extend behind both actual openings. */
  prop(root,p->box,white,x,y+.12f,z+2,15.2f,.24f,4);
  prop(root,p->box,white,x,y+5.15f,z+2.6f,15.2f,1.4f,.65f);
  prop(root,p->box,white,x,y+5.88f,z+2.6f,15.4f,.12f,.8f);
  for(int i=-1;i<=1;i++)prop(root,p->box,white,x+i*7.1f,y+2.25f,z+2.6f,.65f,4.5f,.65f);
  mb arch={0};
  for(int side=-1;side<=1;side+=2) {
    float cx=x+side*3.55f;
    for(int j=0;j<16;j++) {
      float a=-1+2.0f*j/16,b=-1+2.0f*(j+1)/16;
      float xa=cx+a*3.225f,xb=cx+b*3.225f;
      float ya=y+3.65f+.72f*sqrtf(fmaxf(0,1-a*a));
      float yb=y+3.65f+.72f*sqrtf(fmaxf(0,1-b*b));
      int v=mb_v(&arch,(t3_vec3){xa,ya,z+2.93f},(t3_vec3){0,0,1},0,0,1);
      mb_v(&arch,(t3_vec3){xb,yb,z+2.93f},(t3_vec3){0,0,1},1,0,1);
      mb_v(&arch,(t3_vec3){xb,y+4.46f,z+2.93f},(t3_vec3){0,0,1},1,1,1);
      mb_v(&arch,(t3_vec3){xa,y+4.46f,z+2.93f},(t3_vec3){0,0,1},0,1,1);
      mb_t(&arch,v,v+1,v+2);mb_t(&arch,v,v+2,v+3);
      /* Underside gives the porch its visible depth. */
      v=mb_v(&arch,(t3_vec3){xa,ya,z+2.28f},(t3_vec3){0,-1,0},0,0,1);
      mb_v(&arch,(t3_vec3){xb,yb,z+2.28f},(t3_vec3){0,-1,0},1,0,1);
      mb_v(&arch,(t3_vec3){xb,yb,z+2.93f},(t3_vec3){0,-1,0},1,1,1);
      mb_v(&arch,(t3_vec3){xa,ya,z+2.93f},(t3_vec3){0,-1,0},0,1,1);
      mb_t(&arch,v,v+1,v+2);mb_t(&arch,v,v+2,v+3);
    }
    float dx=x+side*2.0f;
    prop(root,p->box,p->cream,dx,y+1.65f,z+.55f,1.45f,3.1f,.12f);
    prop(root,p->box,glass,dx,y+1.65f,z+.64f,1.25f,2.9f,.06f);
    for(int j=0;j<4;j++)prop(root,p->box,p->cream,dx,y+.4f+j*.75f,z+.69f,1.25f,.035f,.025f);
    prop(root,p->box,p->gold,dx+.43f,y+1.55f,z+.73f,.035f,.32f,.04f);
    prop(root,p->box,glass,x+side*5.25f,y+1.8f,z+.58f,1.5f,2.7f,.08f);
    prop(root,p->box,p->black,dx+side*1.1f,y+2.75f,z+.75f,.18f,.38f,.24f);
    prop(root,p->ball,p->lamp,dx+side*1.1f,y+2.75f,z+.9f,.09f,.13f,.07f);
  }
  t3_geometry *g=mb_done(&arch);add(root,g,white,true);t3_release(g);
  prop(root,p->box,p->black,x,y+5.35f,z+2.98f,6.7f,.69f,.07f);
  mesa_name(root,"DIAMOND DONS",x,y+5.35f,z+3.03f,6.3f,.58f);
  mesa_name(root,"LEGENDARY STEAKS & SPIRITS",x,y+4.85f,z+3.03f,6.2f,.31f);
  mesa_logo(root,"signs/diamonddons-official.jpg",x,y+4.29f,z+3.03f,.62f,.62f);
  t3_release(white);t3_release(glass);
}

/* Shared restrained patio furniture; no per-frame updates or lights. */
static void mesa_cafe_table(t3_object *root,const whimsy_parts *p,t3_material *m,
                            float x,float y,float z) {
  prop(root,p->box,m,x,y+1,z,1.5f,.07f,1);
  for(int side=-1;side<=1;side+=2) {
    prop(root,p->box,m,x+side*.55f,y+.5f,z,.05f,1,.75f);
    prop(root,p->box,m,x+side*1.0f,y+.52f,z,.6f,.06f,.6f);
    for(int leg=-1;leg<=1;leg+=2)prop(root,p->box,m,x+side*1.0f,y+.26f,z+leg*.25f,.5f,.52f,.03f);
    for(int bar=0;bar<4;bar++)prop(root,p->box,m,x+side*1.27f,y+.85f,z-.25f+bar*.167f,.03f,.65f,.03f);
    prop(root,p->box,m,x+side*1.27f,y+1.19f,z,.04f,.04f,.6f);
  }
}
static void mesa_margaritas(t3_object *root,const whimsy_parts *p) {
  float x=mesa_layout.margaritas,z=-15,y=bank_y(x,z,2);
  t3_material *wall=mat(TEX_CONCRETE,(float[3]){.86f,.85f,.77f},1,1,.025f,.94f);
  t3_material *olive=toy_paint(.46f,.45f,.22f);olive->roughness=.9f;olive->clearcoat=0;
  t3_material *trim=toy_paint(.29f,.31f,.27f);trim->roughness=.85f;trim->clearcoat=0;
  t3_material *glass=toy_paint(.055f,.09f,.14f);glass->roughness=.27f;glass->metalness=.22f;
  t3_material *tile=toy_paint(.55f,.25f,.15f);tile->roughness=.94f;tile->clearcoat=0;
  prop(root,p->box,wall,x,y+5.5f,z-1,18,11,4);
  prop(root,p->box,olive,x,y+2.15f,z+1.08f,17.6f,4.3f,.2f);
  prop(root,p->box,wall,x,y+5.15f,z+2.8f,22,2.2f,.8f);
  for(int i=0;i<4;i++)prop(root,p->box,wall,x-10.5f+i*7,y+2.2f,z+2.8f,.75f,4.4f,.8f);
  for(int bay=0;bay<3;bay++) {
    float xx=x-5.8f+bay*5.8f;
    prop(root,p->box,trim,xx,y+2.1f,z+1.25f,4.5f,3.85f,.16f);
    for(int j=0;j<3;j++)prop(root,p->box,glass,xx-1.45f+j*1.45f,y+2.1f,z+1.35f,1.37f,3.65f,.06f);
    prop(root,p->box,trim,xx,y+3.1f,z+1.40f,4.4f,.07f,.07f);
    mesa_cafe_table(root,p,trim,xx,y+.15f,z+4.0f);
  }
  /* Upper story: single left window, two pairs, central balcony doors. */
  const float windows[]={-7.0f,-3.9f,-2.4f,4.9f,6.4f};
  for(int i=0;i<5;i++) {
    float xx=x+windows[i];
    prop(root,p->box,trim,xx,y+8.05f,z+1.15f,1.2f,2.75f,.22f);
    prop(root,p->box,glass,xx,y+8.05f,z+1.28f,.91f,2.45f,.06f);
    prop(root,p->box,trim,xx,y+6.65f,z+1.3f,1.45f,.14f,.4f);
  }
  prop(root,p->box,trim,x+1.1f,y+7.98f,z+1.15f,2.65f,2.95f,.22f);
  for(int side=-1;side<=1;side+=2)prop(root,p->box,glass,x+1.1f+side*.61f,y+7.98f,z+1.28f,1.09f,2.66f,.06f);
  prop(root,p->box,wall,x+1.1f,y+6.55f,z+1.7f,4,.16f,1.4f);
  for(int j=0;j<15;j++)prop(root,p->box,trim,x-.8f+j*.27f,y+7.15f,z+2.33f,.025f,1.2f,.025f);
  prop(root,p->box,trim,x+1.1f,y+7.77f,z+2.33f,4,.045f,.045f);
  for(int side=-1;side<=1;side+=2) {
    prop(root,p->box,trim,x+1.1f+side*1.9f,y+7.77f,z+1.76f,.045f,.045f,1.14f);
    for(int j=0;j<4;j++)prop(root,p->box,trim,x+1.1f+side*1.9f,y+7.15f,z+1.35f+j*.27f,.025f,1.2f,.025f);
  }
  /* Two sagging patio strings from the reference. Existing emissive bulbs
   * join the static batches; no shadow lights or runtime animation. */
  for(int j=0;j<4;j++)prop(root,p->box,trim,x-10.5f+j*7,y+1.85f,z+5.05f,.055f,3.7f,.055f);
  for(int row=0;row<2;row++)for(int bay=0;bay<3;bay++) {
    float left=x-10.5f+bay*7,zz=z+3.35f+row*1.7f;
    for(int j=0;j<12;j++) {
      float u=j/12.f,v=(j+1)/12.f;
      float ya=y+3.95f-row*.3f-.38f*4*u*(1-u);
      float yb=y+3.95f-row*.3f-.38f*4*v*(1-v);
      t3_mesh *wire=prop(root,p->box,trim,left+(u+v)*3.5f,(ya+yb)*.5f,zz,
                        sqrtf((7.f/12)*(7.f/12)+(yb-ya)*(yb-ya)),.018f,.018f);
      t3_object_set_rotation(&wire->base,0,0,atan2f(yb-ya,7.f/12));
      if(j%3==1) {
        prop(root,p->box,trim,left+u*7,ya-.09f,zz,.025f,.18f,.025f);
        prop(root,p->ball,p->lamp,left+u*7,ya-.22f,zz,.065f,.10f,.065f);
      }
    }
  }
  prop(root,p->box,trim,x,y+10.5f,z+1.16f,18.3f,.20f,.3f);
  prop(root,p->box,tile,x,y+11.05f,z,17.6f,.2f,2.3f);
  for(int side=-1;side<=1;side+=2)prop(root,p->box,wall,x+side*8.7f,y+11.1f,z,.6f,.7f,2.4f);
  mesa_logo(root,"signs/margaritas-official.png",x,y+5.15f,z+3.23f,9.8f,2.1f);
  for(int j=0;j<51;j++) {
    float dx=-10.5f+j*.42f;if(fabsf(dx)<1)continue;
    prop(root,p->box,trim,x+dx,y+.8f,z+5.3f,.035f,1.5f,.035f);
  }
  for(int side=-1;side<=1;side+=2)prop(root,p->box,trim,x+side*5.8f,y+1.56f,z+5.3f,9.5f,.06f,.06f);
  t3_release(wall);t3_release(olive);t3_release(trim);t3_release(glass);t3_release(tile);
}
/* Opaque folded leaves avoid alpha sorting/overdraw. One reusable shrub
 * mesh, deterministic placement independent of gameplay's random stream. */
static t3_geometry *mesa_shrub_geometry(void) {
  mb leaves={.colors=true};
  for(int i=0;i<192;i++) {
    float a=i*2.39996323f;
    float h=-.85f+1.85f*(i+.5f)/192;
    float r=sqrtf(fmaxf(0,1-h*h));
    float irregular=.78f+.16f*sinf(i*4.17f)+.06f*cosf(i*1.31f);
    t3_vec3 n={r*cosf(a),h,r*sinf(a)};
    t3_vec3 c={n.x*irregular,n.y*irregular,n.z*irregular};
    t3_vec3 u={-sinf(a),0,cosf(a)};
    t3_vec3 v={h*cosf(a),-r,h*sinf(a)};
    float len=.15f+.045f*(1+sinf(i*1.77f)),wide=.09f;
    float shade=.65f+.30f*(.5f+.5f*sinf(i*2.3f));
    int k=mb_v(&leaves,(t3_vec3){c.x+n.x*.045f,c.y+n.y*.045f,c.z+n.z*.045f},n,.5f,.5f,shade);
    mb_v(&leaves,(t3_vec3){c.x+u.x*len,c.y+u.y*len,c.z+u.z*len},n,1,.5f,shade);
    mb_v(&leaves,(t3_vec3){c.x+v.x*wide,c.y+v.y*wide,c.z+v.z*wide},n,.5f,1,shade*.86f);
    mb_v(&leaves,(t3_vec3){c.x-u.x*len,c.y-u.y*len,c.z-u.z*len},n,0,.5f,shade);
    mb_v(&leaves,(t3_vec3){c.x-v.x*wide,c.y-v.y*wide,c.z-v.z*wide},n,.5f,0,shade);
    for(int j=0;j<4;j++)mb_t(&leaves,k,k+1+j,k+1+(j+1)%4);
  }
  return mb_done(&leaves);
}
static void mesa_lost_dutchman(t3_object *root,const whimsy_parts *p) {
  float x=mesa_layout.lost_dutchman,z=-15,y=bank_y(x,z,2);
  t3_material *wall=mat(TEX_CONCRETE,(float[3]){.82f,.77f,.65f},1,1,.06f,.95f);
  t3_material *wood=toy_paint(.34f,.12f,.10f);wood->roughness=.9f;wood->clearcoat=0;
  t3_material *tile=toy_paint(.63f,.25f,.19f);tile->roughness=.94f;tile->clearcoat=0;
  t3_material *glass=toy_paint(.035f,.07f,.08f);glass->roughness=.3f;glass->metalness=.2f;
  prop(root,p->box,wall,x,y+3.7f,z-1,23,7.4f,4);
  prop(root,p->box,wood,x,y+7.42f,z,23,.19f,2.2f);
  for(int j=0;j<6;j++) {
    float xx=x-7.5f+j*3;
    prop(root,p->box,wood,xx,y+2.1f,z+1.10f,1.95f,3.5f,.15f);
    prop(root,p->box,glass,xx,y+2.1f,z+1.2f,1.76f,3.3f,.06f);
  }
  for(int i=0;i<5;i++) {
    float xx=x-9+i*4.5f;
    prop(root,p->box,wall,xx,y+2.45f,z+4.1f,.85f,4.9f,.85f);
    prop(root,p->box,wood,xx,y+4.92f,z+3,.22f,.28f,3.5f);
    t3_mesh *brace=prop(root,p->box,wood,xx,y+4.35f,z+3.65f,.2f,1.25f,.2f);
    t3_object_set_rotation(&brace->base,-.65f,0,0);
  }
  prop(root,p->box,wood,x,y+4.95f,z+4.4f,23,.32f,.35f);
  /* Rounded tile normals and raised course lips, in one static mesh.
   * 48 columns x4 courses x4 facets x4 triangles =3072, same as old roof. */
  mb roof={0};
  const float width=23.2f/48;
  for(int col=0;col<48;col++)for(int row=0;row<4;row++)for(int k=0;k<4;k++) {
    float a=k*3.1415927f/4,b=(k+1)*3.1415927f/4;
    float xa=x-11.6f+(col+k/4.f)*width,xb=xa+width/4;
    float za=z+1.0f+row*.9f,zb=za+.96f;
    float ha=.08f*sinf(a),hb=.08f*sinf(b);
    float ya=y+6.05f-row*.315f,yb=ya-.288f;
    t3_vec3 na={-.08f*3.1415927f/width*cosf(a),1,.3f};
    t3_vec3 nb={-.08f*3.1415927f/width*cosf(b),1,.3f};
    float la=sqrtf(na.x*na.x+1.09f),lb=sqrtf(nb.x*nb.x+1.09f);
    na=(t3_vec3){na.x/la,1/la,.3f/la};nb=(t3_vec3){nb.x/lb,1/lb,.3f/lb};
    int v=mb_v(&roof,(t3_vec3){xa,ya+ha,za},na,0,0,1);
    mb_v(&roof,(t3_vec3){xb,ya+hb,za},nb,1,0,1);
    mb_v(&roof,(t3_vec3){xb,yb+hb,zb},nb,1,1,1);
    mb_v(&roof,(t3_vec3){xa,yb+ha,zb},na,0,1,1);
    mb_t(&roof,v,v+2,v+1);mb_t(&roof,v,v+3,v+2);
    t3_vec3 front={0,0,1};
    v=mb_v(&roof,(t3_vec3){xa,yb+ha,zb},front,0,0,1);
    mb_v(&roof,(t3_vec3){xa,yb+ha-.045f,zb},front,0,1,1);
    mb_v(&roof,(t3_vec3){xb,yb+hb-.045f,zb},front,1,1,1);
    mb_v(&roof,(t3_vec3){xb,yb+hb,zb},front,1,0,1);
    mb_t(&roof,v,v+1,v+2);mb_t(&roof,v,v+2,v+3);
  }
  t3_geometry *g=mb_done(&roof);add(root,g,tile,true);t3_release(g);
  mesa_name(root,"Lost Dutchman Coffee",x,y+6.75f,z+1.18f,15.2f,1.05f);
  prop(root,p->box,wood,x+7.5f,y+1.95f,z+1.14f,2.05f,3.9f,.16f);
  prop(root,p->box,glass,x+7.5f,y+1.95f,z+1.24f,1.85f,3.7f,.06f);
  prop(root,p->box,p->cream,x+8.15f,y+1.7f,z+1.3f,.045f,.38f,.06f);
  prop(root,p->box,p->cream,x+7.5f,y+2.5f,z+1.3f,1.55f,.55f,.025f);
  mesa_logo(root,"signs/lostdutchman-official.png",x+7.5f,y+2.5f,z+1.32f,1.5f,.5f);
  t3_geometry *shrub=mesa_shrub_geometry();
  t3_material *sage=t3_mesh_standard_material_new(0x728174);sage->roughness=1;sage->vertex_colors=true;sage->side=T3_DOUBLE_SIDE;
  t3_material *leaves=t3_mesh_standard_material_new(0x46572b);leaves->roughness=1;leaves->vertex_colors=true;leaves->side=T3_DOUBLE_SIDE;
  for(int i=0;i<12;i++) {
    float xx=x-10+i*1.45f;
    prop(root,shrub,leaves,xx,y+.25f,z+4.85f,.85f,.27f,.45f);
    if(i%2==0)prop(root,shrub,sage,xx,y+.48f,z+4.8f,.57f,.52f,.5f);
  }
  t3_release(shrub);t3_release(sage);t3_release(leaves);
  for(int row=0;row<3;row++)prop(root,p->box,wood,x-1.5f,y+.5f+row*.4f,z+4.5f,20,.06f,.06f);
  for(int i=0;i<9;i++)prop(root,p->box,wood,x-11+i*2.4f,y+.7f,z+4.5f,.07f,1.4f,.07f);
  for(int i=0;i<3;i++)mesa_cafe_table(root,p,wood,x-6+i*6,y+.1f,z+3.1f);
  t3_release(wall);t3_release(wood);t3_release(tile);t3_release(glass);
}

/* Mango's: mint stucco portal, low brick recess and open cafe patio. */
static void mesa_mangos(t3_object *root,const whimsy_parts *p,t3_material *brick) {
  float x=mesa_layout.mangos,z=-14.8f,y=bank_y(x,z,2);
  t3_material *mint=mat(TEX_CONCRETE,(float[3]){.70f,.88f,.76f},1,1,.03f,.94f);
  t3_material *glass=toy_paint(.055f,.10f,.095f);glass->metalness=.2f;glass->roughness=.27f;
  t3_material *metal=toy_paint(.20f,.17f,.12f);metal->roughness=.68f;metal->clearcoat=0;
  t3_material *red=toy_paint(.72f,.16f,.11f);red->roughness=.85f;red->clearcoat=0;
  prop(root,p->box,mint,x,y+3.7f,z-1.6f,14,7.4f,3.6f);
  mesa_masonry(root,brick,x,y+2.4f,z+.6f,11.7f,4.8f,.5f);
  for(int side=-1;side<=1;side+=2)prop(root,p->box,mint,x+side*6.3f,y+2.56f,z+2.3f,1.4f,5.12f,1.1f);
  prop(root,p->box,mint,x,y+6.2f,z+2.3f,14,2.4f,1.1f);
  prop(root,p->box,mint,x,y+7.43f,z+2.3f,14,.09f,1.15f);
  mb arch={0};
  for(int j=0;j<24;j++) {
    float a=-1+j/12.f,b=-1+(j+1)/12.f;
    float xa=x+a*5.6f,xb=x+b*5.6f;
    float ya=y+4.2f+.8f*sqrtf(fmaxf(0,1-a*a)),yb=y+4.2f+.8f*sqrtf(fmaxf(0,1-b*b));
    int v=mb_v(&arch,(t3_vec3){xa,ya,z+2.86f},(t3_vec3){0,0,1},0,0,1);
    mb_v(&arch,(t3_vec3){xb,yb,z+2.86f},(t3_vec3){0,0,1},1,0,1);
    mb_v(&arch,(t3_vec3){xb,y+5.02f,z+2.86f},(t3_vec3){0,0,1},1,1,1);
    mb_v(&arch,(t3_vec3){xa,y+5.02f,z+2.86f},(t3_vec3){0,0,1},0,1,1);
    mb_t(&arch,v,v+1,v+2);mb_t(&arch,v,v+2,v+3);
    v=mb_v(&arch,(t3_vec3){xa,ya,z+1.74f},(t3_vec3){0,-1,0},0,0,1);
    mb_v(&arch,(t3_vec3){xb,yb,z+1.74f},(t3_vec3){0,-1,0},1,0,1);
    mb_v(&arch,(t3_vec3){xb,yb,z+2.86f},(t3_vec3){0,-1,0},1,1,1);
    mb_v(&arch,(t3_vec3){xa,ya,z+2.86f},(t3_vec3){0,-1,0},0,1,1);
    mb_t(&arch,v,v+1,v+2);mb_t(&arch,v,v+2,v+3);
  }
  t3_geometry *ag=mb_done(&arch);add(root,ag,mint,true);t3_release(ag);
  prop(root,p->box,metal,x,y+2.1f,z+.95f,9.6f,4.1f,.14f);
  for(int j=0;j<5;j++)prop(root,p->box,glass,x-3.8f+j*1.9f,y+2.1f,z+1.04f,1.78f,3.95f,.05f);
  prop(root,p->box,metal,x,y+3.55f,z+1.09f,9.6f,.09f,.08f);
  prop(root,p->box,p->cream,x-3.1f,y+1.9f,z+1.13f,.04f,.42f,.06f);
  mesa_logo(root,"signs/mangos-official.png",x,y+6.15f,z+2.92f,7.0f,2.07f);
  for(int table=0;table<3;table++) {
    float tx=x-3.6f+table*3.5f,tz=z+3.7f+(table%2)*.6f;
    prop(root,p->cyl,metal,tx,y+.57f,tz,.045f,1.14f,.045f);
    prop(root,p->cyl,metal,tx,y+1.16f,tz,.90f,.055f,.90f);
    for(int side=-1;side<=1;side+=2) {
      float cx=tx+side*1.15f;
      prop(root,p->box,metal,cx,y+.58f,tz,.6f,.045f,.6f);
      for(int leg=-1;leg<=1;leg+=2)prop(root,p->box,metal,cx,y+.28f,tz+leg*.26f,.5f,.56f,.025f);
      for(int slat=0;slat<4;slat++)prop(root,p->box,metal,cx+side*.28f,y+.93f,tz-.25f+slat*.167f,.025f,.7f,.025f);
      prop(root,p->box,metal,cx+side*.28f,y+1.3f,tz,.035f,.035f,.6f);
    }
  }
  mesa_patio_umbrella(root,p,red,x+3.5f,y+3.2f,z+3.6f);
  prop(root,p->cyl,metal,x+3.5f,y+1.6f,z+3.6f,.035f,3.2f,.035f);
  t3_release(mint);t3_release(glass);t3_release(metal);t3_release(red);
}

/* Expanded Espiritu frontage: photo-based shallow stucco arcade and patio,
 * with the official angular wordmark (not the separate fish emblem). */
static void mesa_espiritu(t3_object *root,const whimsy_parts *p) {
  float x=mesa_layout.espiritu,z=-15,y=bank_y(x,z,2);
  t3_material *white=mat(TEX_CONCRETE,(float[3]){.74f,.77f,.80f},1,1,.035f,.94f);
  t3_material *glass=toy_paint(.055f,.09f,.12f);glass->roughness=.26f;glass->metalness=.22f;
  t3_material *cloth=toy_paint(.65f,.59f,.48f);cloth->roughness=.96f;cloth->clearcoat=0;
  prop(root,p->box,white,x,y+3.3f,z-1.5f,22,6.6f,4);
  prop(root,p->box,white,x,y+.10f,z+2.8f,22,.2f,5.6f);
  prop(root,p->box,white,x,y+5.45f,z+2.6f,22,2.1f,.65f);
  prop(root,p->box,white,x,y+6.53f,z+2.6f,22,.08f,.75f);
  for(int i=0;i<4;i++)prop(root,p->box,white,x-10.65f+i*7.1f,y+2.25f,z+2.6f,.65f,4.5f,.65f);
  mb arch={0};
  for(int bay=0;bay<3;bay++) {
    float cx=x+(bay-1)*7.1f;
    for(int j=0;j<16;j++) {
      float a=-1+2.0f*j/16,b=-1+2.0f*(j+1)/16;
      float xa=cx+a*3.225f,xb=cx+b*3.225f;
      float ya=y+3.65f+.72f*sqrtf(fmaxf(0,1-a*a));
      float yb=y+3.65f+.72f*sqrtf(fmaxf(0,1-b*b));
      int v=mb_v(&arch,(t3_vec3){xa,ya,z+2.93f},(t3_vec3){0,0,1},0,0,1);
      mb_v(&arch,(t3_vec3){xb,yb,z+2.93f},(t3_vec3){0,0,1},1,0,1);
      mb_v(&arch,(t3_vec3){xb,y+4.46f,z+2.93f},(t3_vec3){0,0,1},1,1,1);
      mb_v(&arch,(t3_vec3){xa,y+4.46f,z+2.93f},(t3_vec3){0,0,1},0,1,1);
      mb_t(&arch,v,v+1,v+2);mb_t(&arch,v,v+2,v+3);
      /* Underside gives the porch its visible depth. */
      v=mb_v(&arch,(t3_vec3){xa,ya,z+2.28f},(t3_vec3){0,-1,0},0,0,1);
      mb_v(&arch,(t3_vec3){xb,yb,z+2.28f},(t3_vec3){0,-1,0},1,0,1);
      mb_v(&arch,(t3_vec3){xb,yb,z+2.93f},(t3_vec3){0,-1,0},1,1,1);
      mb_v(&arch,(t3_vec3){xa,ya,z+2.93f},(t3_vec3){0,-1,0},0,1,1);
      mb_t(&arch,v,v+1,v+2);mb_t(&arch,v,v+2,v+3);
    }

    prop(root,p->box,p->cream,cx,y+1.9f,z+.56f,5.8f,3.6f,.12f);
    for(int j=0;j<4;j++)prop(root,p->box,glass,cx-2.1f+j*1.4f,y+1.9f,z+.65f,1.32f,3.4f,.05f);
    prop(root,p->box,p->cream,cx,y+2.75f,z+.70f,5.8f,.06f,.04f);
    prop(root,p->box,p->cream,cx+.45f,y+1.6f,z+.73f,.04f,.35f,.06f);
    prop(root,p->box,p->black,cx,y+1.05f,z+3.85f,1.9f,.075f,1.1f);
    for(int side=-1;side<=1;side+=2) {
      prop(root,p->box,p->black,cx+side*.7f,y+.52f,z+3.85f,.04f,1.04f,.8f);
      prop(root,p->box,p->black,cx+side*1.25f,y+.55f,z+3.85f,.6f,.06f,.65f);
      prop(root,p->box,p->black,cx+side*1.50f,y+.85f,z+3.85f,.04f,.65f,.65f);
      for(int leg=-1;leg<=1;leg+=2)prop(root,p->box,p->black,cx+side*1.25f,y+.27f,z+3.85f+leg*.27f,.5f,.54f,.035f);
    }
    /* Closed fabric umbrellas from the reference; no large sign occluders. */
    prop(root,p->cyl,p->black,cx+.65f,y+1.85f,z+4.3f,.035f,3.7f,.035f);
    mb folds={0};
    for(int j=0;j<16;j++) {
      float a=j*.392699082f,b=(j+1)*.392699082f;
      float ra=(j%2)?.28f:.36f,rb=((j+1)%2)?.28f:.36f;
      t3_vec3 n={cosf((a+b)*.5f),.15f,sinf((a+b)*.5f)};
      int v=mb_v(&folds,(t3_vec3){cx+.65f,y+3.45f,z+4.3f},n,.5f,0,1);
      mb_v(&folds,(t3_vec3){cx+.65f+rb*cosf(b),y+1.55f,z+4.3f+rb*sinf(b)},n,1,1,1);
      mb_v(&folds,(t3_vec3){cx+.65f+ra*cosf(a),y+1.55f,z+4.3f+ra*sinf(a)},n,0,1,1);
      mb_t(&folds,v,v+1,v+2);
    }
    t3_geometry *fg=mb_done(&folds);add(root,fg,cloth,true);t3_release(fg);
  }
  t3_geometry *g=mb_done(&arch);add(root,g,white,true);t3_release(g);
  for(int j=0;j<49;j++) {
    float dx=-10.6f+j*.442f;
    if(fabsf(dx)<1)continue;
    prop(root,p->box,p->black,x+dx,y+.7f,z+5.4f,.035f,1.4f,.035f);
  }
  for(int side=-1;side<=1;side+=2) {
    prop(root,p->box,p->black,x+side*5.85f,y+1.4f,z+5.4f,9.7f,.06f,.06f);
    prop(root,p->box,p->black,x+side*10.6f,y+1.4f,z+3.95f,.06f,.06f,2.9f);
  }
  /* GIF first-frame lettering; convert its ink to glow once during loading. */
  prop(root,p->box,p->black,x,y+5.45f,z+2.98f,8.1f,2.15f,.06f);
  mesa_logo(root,"signs/espiritu-wordmark.png",x,y+5.45f,z+3.03f,7.8f,3.10f);
  for(int j=0;j<17;j++) {
    float u=j/16.f;
    if(j<16) {
      float next=(j+1)/16.f;
      float dy=-.55f*(sinf(next*3.1415927f)-sinf(u*3.1415927f));
      t3_mesh *wire=prop(root,p->box,p->black,x-10.5f+21*(u+next)*.5f,
        y+4.55f-.275f*(sinf(u*3.1415927f)+sinf(next*3.1415927f)),z+4.8f,
        sqrtf(1.3125f*1.3125f+dy*dy),.018f,.018f);
      t3_object_set_rotation(&wire->base,0,0,atan2f(dy,1.3125f));
    }
    prop(root,p->ball,p->lamp,x-10.5f+21*u,y+4.55f-.55f*sinf(u*3.1415927f),z+4.8f,.055f,.085f,.055f);
  }
  t3_release(white);t3_release(glass);t3_release(cloth);
}

/* Hope's: white surround, recessed glazing and a horizontal timber fascia.
 * Sign lettering follows the clear photo; official artwork is on the door. */
static void mesa_hopes(t3_object *root,const whimsy_parts *p) {
  float x=mesa_layout.hopes,z=-14.5f,y=bank_y(x,z,2);
  t3_material *white=mat(TEX_CONCRETE,(float[3]){.91f,.9f,.85f},1,1,.04f,.85f);
  t3_material *wood=toy_paint(.30f,.13f,.085f);
  t3_material *wood2=toy_paint(.39f,.19f,.12f);
  t3_material *glass=toy_paint(.055f,.12f,.16f);glass->roughness=.22f;glass->metalness=.32f;
  t3_material *teal=toy_paint(.025f,.61f,.72f);
  prop(root,p->box,white,x,y+3.9f,z-1.2f,18,7.8f,3.4f);
  /* Wide white border extends forward around the recessed entrance. */
  prop(root,p->box,white,x,y+7.1f,z+2,18,1.4f,1.3f);
  for(int side=-1;side<=1;side+=2)
    prop(root,p->box,white,x+side*7.9f,y+3.2f,z+2,2.2f,6.4f,1.3f);
  prop(root,p->box,p->black,x,y+2.25f,z+1.82f,13.6f,4.5f,.16f);
  for(int j=0;j<7;j++) {
    float gx=x-5.85f+j*1.95f;
    prop(root,p->box,glass,gx,y+1.88f,z+1.94f,1.85f,3.5f,.08f);
    prop(root,p->box,glass,gx,y+4.03f,z+1.94f,1.85f,.65f,.08f);
  }
  prop(root,p->box,p->black,x,y+3.65f,z+2,13.6f,.085f,.12f);
  prop(root,p->box,p->cream,x+4.5f,y+1.65f,z+2.04f,.045f,.55f,.06f);
  /* A single textured slab avoids subpixel gaps and per-board shadow acne. */
  wood->roughness=wood2->roughness=.83f;
  wood->clearcoat=wood2->clearcoat=0;
  t3_material *fascia=mat(TEX_WOOD,(float[3]){.62f,.36f,.25f},1,1,.08f,.85f);
  prop(root,p->box,fascia,x,y+5.3925f,z+2.8f,15.2f,1.613f,.32f);
  t3_release(fascia);
  /* Angular turquoise emblem: sloping notches, not rectangular blocks. */
  const float outline[][2]={{-.64f,.73f},{.64f,.73f},{.38f,.49f},{.88f,.49f},
    {.62f,.25f},{1.18f,.25f},{.91f,0},{1.18f,-.25f},{.62f,-.25f},
    {.88f,-.49f},{.38f,-.49f},{.64f,-.73f},{-.64f,-.73f},
    {-.38f,-.49f},{-.88f,-.49f},{-.62f,-.25f},{-1.18f,-.25f},
    {-.91f,0},{-1.18f,.25f},{-.62f,.25f},{-.88f,.49f},{-.38f,.49f}};
  mb emblem={0};
  int center=mb_v(&emblem,(t3_vec3){x-4.15f,y+5.42f,z+3.15f},(t3_vec3){0,0,1},.5f,.5f,1);
  for(int j=0;j<22;j++)mb_v(&emblem,(t3_vec3){x-4.15f+outline[j][0],y+5.42f+outline[j][1],z+3.15f},(t3_vec3){0,0,1},0,0,1);
  for(int j=0;j<22;j++)mb_t(&emblem,center,center+1+(j+1)%22,center+1+j);
  t3_geometry *badge=mb_done(&emblem);add(root,badge,teal,false);t3_release(badge);
  mesa_name(root,"HOPE'S",x-4.15f,y+5.42f,z+3.16f,2.1f,.55f);
  mesa_name(root,"FRYBREAD",x+1.4f,y+5.42f,z+3.12f,8.1f,1.65f);
  mesa_logo(root,"signs/hopes-official.png",x+4.9f,y+2.5f,z+2.02f,.85f,.56f);
  /* Window neon from the reference; the main turquoise fascia is painted. */
  mesa_name(root,"OPEN",x,y+2.7f,z+2.04f,1.25f,.62f);
  t3_material *cyan=t3_mesh_basic_material_new(0x36bfff);cyan->color=(t3_color){.3f,2.5f,4};
  for(int side=-1;side<=1;side+=2)for(int arm=-1;arm<=1;arm+=2) {
    t3_mesh *tube=prop(root,p->box,cyan,x+arm*.29f,y+2.7f+side*.49f,z+2.05f,.78f,.027f,.025f);
    t3_object_set_rotation(&tube->base,0,0,-side*arm*.68f);
  }
  t3_release(cyan);
  /* Small outdoor shelf and cafe table, below the window sightline. */
  for(int j=0;j<3;j++)prop(root,p->box,wood2,x+1.4f,y+.25f+j*.48f,z+2.8f,1.35f,.07f,.48f);
  for(int side=-1;side<=1;side+=2)prop(root,p->box,wood2,x+1.4f+side*.66f,y+.8f,z+2.8f,.045f,1.6f,.48f);
  prop(root,p->cyl,p->black,x-2.6f,y+.6f,z+3.3f,.065f,1.2f,.065f);
  prop(root,p->cyl,wood2,x-2.6f,y+1.2f,z+3.3f,.75f,.08f,.75f);
  t3_release(white);t3_release(wood);t3_release(wood2);t3_release(glass);t3_release(teal);
}

/* Casa Copal: reconstructed mural uses one bitmap; painted sign silhouette
 * and stacked projecting name panels follow the supplied storefront photo. */
static void mesa_casa_copal(t3_object *root,const whimsy_parts *p) {
  float x=mesa_layout.casa_copal,z=-14.5f,y=bank_y(x,z,2);
  t3_material *beige=mat(TEX_CONCRETE,(float[3]){.68f,.58f,.43f},1,1,.05f,.9f);
  t3_material *terra=toy_paint(.69f,.32f,.19f);
  t3_material *blue=toy_paint(.10f,.24f,.34f);
  t3_material *purple=toy_paint(.39f,.23f,.39f);
  t3_material *glass=toy_paint(.095f,.13f,.12f);glass->roughness=.3f;glass->metalness=.18f;
  prop(root,p->box,beige,x,y+2.6f,z-.3f,10.8f,5.2f,4.3f);
  prop(root,p->box,p->black,x,y+1.6f,z+1.91f,9.8f,3.2f,.12f);
  for(int j=0;j<5;j++)prop(root,p->box,glass,x-3.8f+j*1.9f,y+1.6f,z+2.01f,1.78f,3.04f,.06f);
  prop(root,p->box,terra,x,y+3.85f,z+2.45f,10.8f,1.65f,.8f);
  mesa_logo(root,"signs/casacopal-mural-reconstructed.png",x,y+3.85f,z+2.87f,10.8f,1.65f);
  for(int side=-1;side<=1;side+=2) {
    prop(root,p->box,terra,x+side*4.9f,y+1.5f,z+2.4f,.95f,3,.85f);
    mesa_logo(root,"signs/casacopal-mural-reconstructed.png",x+side*4.9f,y+1.5f,z+2.84f,.95f,3);
    t3_mesh *panel=(t3_mesh *)root->children[root->child_count-1];
    float *uv=panel->geometry->attributes[T3_ATTR_UV]->array;
    for(int v=0;v<panel->geometry->attributes[T3_ATTR_UV]->count;v++)uv[v*2]=.10f+uv[v*2]*.10f;
  }
  prop(root,p->box,p->cream,x,y+4.72f,z+2.4f,10.95f,.12f,.95f);
  mesa_logo(root,"signs/casacopal-official.png",x+.8f,y+2.0f,z+2.06f,1.05f,.76f);
  /* Raised asymmetric upright cabinet with rounded top and painted edging. */
  float sx=x+3.2f;
  mb face={0};
  const t3_vec2 outline[]={{-.5f,0},{.85f,0},{1.05f,2.9f},{.96f,3.6f},{.66f,4.1f},{.15f,4.5f},{-.45f,4.7f},{-.95f,4.55f},{-1.17f,4.15f},{-1.1f,3.5f}};
  int n=sizeof outline/sizeof outline[0];
  for(int j=0;j<n;j++)mb_v(&face,(t3_vec3){outline[j].x,outline[j].y,0},(t3_vec3){0,0,1},0,0,1);
  /* Convex silhouette fan. */
  for(int j=1;j<n-1;j++)mb_t(&face,0,j,j+1);
  t3_geometry *g=mb_done(&face);
  t3_mesh *rim=add(root,g,terra,true);rim->base.position=(t3_vec3){sx,y+4.4f,z+2};rim->base.scale=(t3_vec3){1.1f,1.025f,1};
  t3_mesh *front=add(root,g,blue,true);front->base.position=(t3_vec3){sx,y+4.45f,z+2.1f};t3_release(g);
  prop(root,p->ball,purple,sx+.2f,y+7.6f,z+2.13f,.68f,.8f,.06f);
  prop(root,p->ball,p->gold,sx-.55f,y+8.5f,z+2.15f,.32f,.42f,.05f);
  for(int j=0;j<2;j++) {
    float sy=y+6.7f-j*.9f;
    prop(root,p->box,terra,sx-.62f,sy,z+2.24f,2.6f,.72f,.4f);
    prop(root,p->box,j?purple:blue,sx-.5f,sy,z+2.47f,2.5f,.7f,.08f);
    mesa_name(root,j?"COPAL":"CASA",sx-.5f,sy,z+2.53f,2.35f,.65f);
  }
  t3_release(beige);t3_release(terra);t3_release(blue);t3_release(purple);t3_release(glass);
}

/* Last landmark: Guadalupe's broad white wings and copper gabled entry. */
static void mesa_guadalupe(t3_object *root,const whimsy_parts *p) {
  float x=mesa_layout.guadalupe,z=-16,y=bank_y(x,z,2);
  t3_material *white=mat(TEX_CONCRETE,(float[3]){.88f,.88f,.82f},1,1,.04f,.9f);
  t3_material *stone=toy_paint(.22f,.23f,.20f);
  t3_material *copper=toy_paint(.66f,.36f,.23f);
  t3_material *roof=toy_paint(.23f,.25f,.24f);roof->metalness=.3f;roof->roughness=.65f;
  t3_material *glass=toy_paint(.07f,.13f,.14f);glass->roughness=.25f;glass->metalness=.22f;
  t3_material *green=toy_paint(.24f,.35f,.13f);green->roughness=1;green->metalness=0;
  prop(root,p->box,white,x,y+2.9f,z,24,5.8f,6);
  prop(root,p->box,copper,x,y+5.85f,z,24.3f,.24f,6.2f);
  for(int i=-5;i<=5;i++) {
    prop(root,p->box,p->cream,x+i*2.25f,y+2.9f,z+3.05f,.22f,5.8f,.18f);
    if((i>1&&i<5)||i<-3)prop(root,p->box,glass,x+i*2.25f+1,y+1.7f,z+3.11f,1.65f,3.2f,.08f);
  }
  float ex=x-3.4f;
  prop(root,p->box,stone,ex,y+3.3f,z+2.3f,7.1f,6.6f,4);
  prop(root,p->box,copper,ex,y+6.65f,z+2.3f,7.3f,.2f,4.2f);
  prop(root,p->box,p->cream,ex,y+2.05f,z+4.35f,4,4.1f,.12f);
  for(int i=-1;i<=1;i+=2)prop(root,p->box,glass,ex+i*.98f,y+2.05f,z+4.44f,1.86f,3.94f,.08f);
  prop(root,p->box,p->cream,ex,y+2.6f,z+4.5f,4,.08f,.06f);
  /* The reference entrance has a glazed triangular transom below the gable. */
  mb transom={0};
  int tv=mb_v(&transom,(t3_vec3){-1.92f,4.15f,0},(t3_vec3){0,0,1},0,0,1);
  mb_v(&transom,(t3_vec3){1.92f,4.15f,0},(t3_vec3){0,0,1},1,0,1);
  mb_v(&transom,(t3_vec3){0,5.95f,0},(t3_vec3){0,0,1},.5f,1,1);
  mb_t(&transom,tv,tv+1,tv+2);
  t3_geometry *tg=mb_done(&transom);prop(root,tg,glass,ex,y,z+4.46f,1,1,1);t3_release(tg);
  prop(root,p->box,roof,ex,y+5.05f,z+4.52f,.09f,1.8f,.08f);
  for(int side=-1;side<=1;side+=2) {
    t3_mesh *frame=prop(root,p->box,roof,ex+side*.96f,y+5.05f,z+4.52f,2.64f,.09f,.08f);
    t3_object_set_rotation(&frame->base,0,0,-side*.75315f);
  }
  /* Two pitched planes give a real gable, with projecting copper fascia. */
  for(int side=-1;side<=1;side+=2) {
    t3_mesh *r=prop(root,p->box,roof,ex+side*2.2f,y+5.55f,z+4.05f,4.8f,.18f,5.2f);
    t3_object_set_rotation(&r->base,0,0,-side*.38f);
    r=prop(root,p->box,copper,ex+side*2.2f,y+5.42f,z+6.7f,4.8f,.53f,.18f);
    t3_object_set_rotation(&r->base,0,0,-side*.38f);
    prop(root,p->box,stone,ex+side*4.3f,y+2.35f,z+5.7f,.24f,4.7f,.24f);
  }
  mesa_logo(root,"signs/guadalupe-official.png",ex-3.7f,y+2.75f,z+4.36f,2.6f,1.9f);
  /* Thin, tapered agave leaves instead of the old round shrub/cone clusters. */
  mb agave={0};
  for(int leaf=0;leaf<9;leaf++) {
    float angle=leaf*2.399963f,c=cw_cos(angle),s=cw_sin(angle);
    float reach=.55f+.12f*(leaf%3),rise=.72f+.12f*(leaf%2);
    for(int segment=0;segment<3;segment++) {
      t3_vec3 edge[4];
      for(int k=0;k<4;k++) {
        float t=(segment+(k==1||k==2))/3.f;
        float w=(.1f*cw_sin(t*3.1415927f)+.025f*(1-t))*(k<2?-1:1);
        edge[k]=(t3_vec3){c*reach*t-s*w,.02f+rise*t-.12f*t*t,s*reach*t+c*w};
      }
      if(segment==2) {
        t3_vec3 a=edge[0],b=edge[1],d=edge[3];
        t3_vec3 u={b.x-a.x,b.y-a.y,b.z-a.z},v={d.x-a.x,d.y-a.y,d.z-a.z};
        t3_vec3 n={u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
        for(int side=-1;side<=1;side+=2) {
          t3_vec3 normal={n.x*side,n.y*side,n.z*side};
          int first=mb_v(&agave,a,normal,0,0,1);mb_v(&agave,b,normal,.5f,1,1);mb_v(&agave,d,normal,1,0,1);
          mb_t(&agave,first,first+(side>0?1:2),first+(side>0?2:1));
        }
      } else {
        circus_quad(&agave,edge[0],edge[1],edge[2],edge[3]);
        circus_quad(&agave,edge[3],edge[2],edge[1],edge[0]);
      }
    }
  }
  t3_geometry *ag=mb_done(&agave);
  for(int side=-1;side<=1;side+=2) {
    float px=x+side*7.6f;
    prop(root,p->box,stone,px,y+.55f,z+6.0f,5.4f,1.1f,1.5f);
    prop(root,p->box,p->brown,px,y+1.11f,z+6.0f,5.1f,.08f,1.25f);
    for(int j=0;j<5;j++) {
      float sc=.75f+.11f*(j%3);
      t3_mesh *plant=prop(root,ag,green,px-2+j,y+1.15f,z+6,sc,sc,sc);
      t3_object_set_rotation(&plant->base,0,j*1.7f,0);
    }
  }
  t3_release(ag);
  t3_release(white);t3_release(stone);t3_release(copper);t3_release(roof);t3_release(glass);t3_release(green);
}

/* República Empanada is set back behind a desert garden, with a roadside
 * burgundy blade rather than a large wordmark across the white parapet. */
static void mesa_republica(t3_object *root,const whimsy_parts *p) {
  float x=mesa_layout.republica,z=-18,y=bank_y(x,z,2);
  t3_material *white=mat(TEX_CONCRETE,(float[3]){.88f,.87f,.79f},1,1,.03f,.92f);
  t3_material *wine=toy_paint(.43f,.10f,.22f);wine->roughness=.9f;
  t3_material *green=toy_paint(.27f,.36f,.12f);green->roughness=1;green->metalness=0;
  t3_material *glass=toy_paint(.07f,.12f,.11f);glass->roughness=.28f;glass->metalness=.2f;
  prop(root,p->box,white,x,y+2.35f,z,16,4.7f,5.5f);
  prop(root,p->box,white,x,y+4.75f,z,16.2f,.16f,5.7f);
  for(int j=0;j<3;j++) {
    float wx=x-4.9f+j*4.5f;
    prop(root,p->box,p->cream,wx,y+2.5f,z+2.82f,3.35f,1.6f,.12f);
    prop(root,p->box,glass,wx,y+2.5f,z+2.9f,3.15f,1.4f,.06f);
    t3_mesh *a=prop(root,p->box,wine,wx,y+3.5f,z+3.25f,3.6f,.08f,1.25f);
    t3_object_set_rotation(&a->base,.32f,0,0);
    prop(root,p->box,wine,wx,y+3.28f,z+3.85f,3.6f,.44f,.08f);
  }
  prop(root,p->box,p->black,x+6.7f,y+1.9f,z+2.8f,1.45f,3.8f,.12f);
  for(int j=0;j<6;j++)prop(root,p->box,p->cream,x+6.05f+j*.26f,y+1.9f,z+2.9f,.035f,3.8f,.06f);
  prop(root,p->box,p->cream,x+6.7f,y+2.3f,z+2.92f,1.45f,.04f,.06f);
  /* Sign near the sidewalk, with pale edge trim and official rooster mark. */
  float sx=x-9.8f,sz=z+7.0f;
  prop(root,p->box,p->cream,sx,y+2.5f,sz,1.4f,4.9f,.25f);
  prop(root,p->box,wine,sx,y+2.5f,sz+.15f,1.25f,4.7f,.12f);
  prop(root,p->ball,p->cream,sx,y+4.75f,sz,.86f,.86f,.14f);
  prop(root,p->ball,wine,sx,y+4.75f,sz+.12f,.79f,.79f,.09f);
  mesa_logo(root,"signs/republica-official.png",sx,y+4.75f,sz+.23f,1.52f,1.52f);
  mesa_name(root,"EMPANADA",sx,y+2.4f,sz+.24f,1.1f,.35f);
  /* Organ-pipe cactus clumps keep the sign clear and use shared primitives. */
  for(int side=-1;side<=1;side+=2) {
    float cx=x+side*5.9f,cz=z+5.3f;
    for(int j=0;j<7;j++) {
      float h=1.15f+(j%3)*.48f,dx=(j%3-1)*.35f,dz=(j/3-1)*.32f;
      prop(root,p->cyl,green,cx+dx,y+h*.5f,cz+dz,.12f,h,.12f);
      prop(root,p->ball,green,cx+dx,y+h,cz+dz,.12f,.14f,.12f);
    }
    for(int j=0;j<5;j++) {
      float dx=(j-2)*.35f;
      prop(root,p->ball,green,cx+dx+side*1.1f,y+.3f,cz+.5f,.34f,.28f,.35f);
      prop(root,p->ball,wine,cx+dx+side*1.1f,y+.52f,cz+.6f,.07f,.06f,.07f);
    }
  }
  t3_release(white);t3_release(wine);t3_release(green);t3_release(glass);
}

/* Pizza Mart: steep entrance A-frame projects from the long sloping roof.
 * Photo-inspired reconstructed yellow script sign uses the existing bloom. */
static void mesa_pizza_mart(t3_object *root,const whimsy_parts *p) {
  float x=mesa_layout.pizza_mart,z=-16,y=bank_y(x,z,2),ex=x-5;
  t3_material *brown=toy_paint(.28f,.17f,.1f);brown->roughness=.87f;
  t3_material *roof=toy_paint(.53f,.54f,.43f);roof->roughness=.85f;
  t3_material *glass=toy_paint(.08f,.10f,.09f);glass->roughness=.25f;glass->metalness=.15f;
  t3_material *masonry=t3_mesh_standard_material_new(0xffffff);
  masonry->roughness=.95f;
  /* Warm red brick from the reference, with pale mortar and varied courses. */
  unsigned char blocks[128*128*4];
  for(int py=0;py<128;py++)for(int px=0;px<128;px++) {
    bool joint=py%16<2||(px+(py/16%2)*16)%32<2;
    unsigned h=sh((unsigned)(px/32+py/16*29));
    int at=(py*128+px)*4,v=joint?82:148+(int)(h%19)+(int)(sh(px+py*128)%9)-4;
    blocks[at]=joint?133:v+22;blocks[at+1]=joint?121:v*58/100;blocks[at+2]=joint?104:v*42/100;blocks[at+3]=255;
  }
  t3_texture *blockmap=t3_texture_new(128,128,blocks);
  blockmap->color_space=T3_SRGB_COLOR_SPACE;blockmap->wrap_s=blockmap->wrap_t=T3_REPEAT;
  blockmap->repeat=(t3_vec2){1.65f,.8f};blockmap->generate_mipmaps=true;blockmap->min_filter=T3_LINEAR_MIPMAP_LINEAR;
  t3_material_set_map(masonry,blockmap);t3_release(blockmap);
  t3_material *foliage=t3_mesh_standard_material_new(0x30442b);foliage->roughness=1;
  t3_material *stone=t3_mesh_standard_material_new(0x94765a);stone->roughness=1;
  t3_geometry *clump=t3_icosahedron_geometry_new(1,1);
  t3_geometry *rock=t3_icosahedron_geometry_new(1,0);
  mesa_masonry(root,masonry,x,y+2.05f,z,21,4.1f,6);
  for(int j=0;j<9;j++) {
    float wx=x-9+j*2.25f;
    prop(root,p->box,p->cream,wx,y+2.2f,z+3.05f,1.9f,1.65f,.14f);
    prop(root,p->box,glass,wx,y+2.2f,z+3.15f,1.7f,1.47f,.05f);
    prop(root,p->box,p->brown,wx,y+2.2f,z+3.19f,.05f,1.5f,.05f);
    prop(root,p->box,p->lamp,wx,y+2.85f,z+3.2f,1.35f,.035f,.04f);
  }
  /* Main ridge runs along the frontage; horizontal courses on its slope. */
  for(int side=-1;side<=1;side+=2) {
    t3_mesh *r=prop(root,p->box,roof,x,y+5.2f,z+side*1.85f,21.7f,.18f,4.8f);
    t3_object_set_rotation(&r->base,side*.62f,0,0);
    for(int j=0;j<14;j++) {
      float along=-2.4f+(j+.5f)*4.8f/14;
      prop(root,p->box,brown,x,y+5.2f-along*cw_sin(.62f)+.105f*cw_cos(.62f),
           z+side*(1.85f+along*cw_cos(.62f)+.105f*cw_sin(.62f)),21.7f,.025f,.04f);
    }
  }
  /* Entrance triangle: two long roof wings down nearly to ground level. */
  for(int side=-1;side<=1;side+=2) {
    t3_mesh *r=prop(root,p->box,roof,ex+side*1.7f,y+3.75f,z+3.6f,7.15f,.18f,3.2f);
    t3_object_set_rotation(&r->base,0,0,-side*1.075f);
    for(int row=0;row<18;row++) {
      float along=-3.575f+(row+.5f)*7.15f/18,angle=-side*1.075f;
      t3_mesh *course=prop(root,p->box,brown,
          ex+side*1.7f+along*cw_cos(angle)-.105f*cw_sin(angle),
          y+3.75f+along*cw_sin(angle)+.105f*cw_cos(angle),z+3.6f,.035f,.025f,3.2f);
      t3_object_set_rotation(&course->base,0,0,angle);
    }
    r=prop(root,p->box,brown,ex+side*1.7f,y+3.75f,z+5.26f,7.3f,.27f,.22f);
    t3_object_set_rotation(&r->base,0,0,-side*1.075f);
  }
  /* Dark triangular gable closes the entry behind the projecting beams. */
  mb gable={0};
  mb_v(&gable,(t3_vec3){ex-3.4f,y+.5f,z+5.05f},(t3_vec3){0,0,1},0,0,1);
  mb_v(&gable,(t3_vec3){ex+3.4f,y+.5f,z+5.05f},(t3_vec3){0,0,1},1,0,1);
  mb_v(&gable,(t3_vec3){ex,y+7,z+5.05f},(t3_vec3){0,0,1},.5f,1,1);
  mb_t(&gable,0,1,2);t3_geometry *g=mb_done(&gable);add(root,g,brown,true);t3_release(g);
  prop(root,p->box,p->cream,ex,y+1.55f,z+5.10f,2.5f,3.1f,.1f);
  prop(root,p->box,glass,ex,y+1.55f,z+5.18f,2.3f,2.9f,.05f);
  prop(root,p->box,brown,ex,y+1.55f,z+5.23f,.1f,3,.06f);
  prop(root,p->box,p->cream,ex+.25f,y+1.5f,z+5.27f,.04f,.45f,.05f);
  /* Windows on both sides of the entrance leave the steep gable readable. */
  for(int side=-1;side<=1;side+=2) {
    float wx=ex+side*1.83f;
    prop(root,p->box,brown,wx,y+1.85f,z+5.12f,.92f,1.36f,.1f);
    prop(root,p->box,glass,wx,y+1.85f,z+5.2f,.79f,1.2f,.05f);
    prop(root,p->box,p->cream,wx,y+1.25f,z+5.25f,.95f,.06f,.12f);
    /* Small colored window posters, subordinate to the roof sign. */
    prop(root,p->box,side<0?p->gold:p->red,wx,y+1.95f,z+5.25f,.28f,.45f,.015f);
    for(int j=0;j<3;j++) {
      float bx=ex+side*(2.7f+j*.38f),bz=z+5.4f+(j%2)*.18f;
      prop(root,clump,foliage,bx,y+.4f,bz,.55f,.48f,.55f);
      t3_mesh *r=prop(root,rock,stone,bx+side*.28f,y+.12f,bz+.55f,.32f,.22f,.26f);
      t3_object_set_rotation(&r->base,.2f*j,.7f*j,.3f);
    }
  }
  /* Low clipped hedges and irregular rocks along the window wing. */
  for(int j=0;j<4;j++) {
    float bx=x+.5f+j*2.6f;
    for(int k=0;k<3;k++)prop(root,clump,foliage,bx+(k-1)*.35f,y+.38f,z+3.9f,.42f,.45f,.44f);
    t3_mesh *r=prop(root,rock,stone,bx+.65f,y+.12f,z+4.2f,.3f,.23f,.25f);
    t3_object_set_rotation(&r->base,.4f,j*.8f,.3f);
  }
  for(int side=-1;side<=1;side+=2)prop(root,p->box,brown,ex+side*1.7f,y+7.35f,z+3.4f,.09f,.8f,.09f);
  prop(root,p->box,brown,ex,y+8.15f,z+3.4f,5.8f,1.7f,.4f);
  mesa_logo(root,"signs/pizzamart-reconstructed.png",ex,y+8.15f,z+3.63f,5.5f,1.45f);
  /* Matching low wing sign, visible from the far side of the entrance. */
  prop(root,p->box,brown,x+6.5f,y+5.1f,z+3.3f,5.5f,1.25f,.3f);
  mesa_logo(root,"signs/pizzamart-reconstructed.png",x+6.5f,y+5.1f,z+3.48f,5.2f,1.05f);
  t3_release(brown);t3_release(roof);t3_release(glass);
  t3_release(masonry);t3_release(foliage);t3_release(stone);t3_release(clump);t3_release(rock);
}

/* Tessellated fabric with a shallow sag; both faces share the existing cloth
 * material, so viewing the patio from a jump needs no new shader variant. */
static t3_vec3 mesa_sail_point(t3_vec3 a,t3_vec3 b,t3_vec3 c,float u,float v) {
  float w=1-u-v;
  return (t3_vec3){a.x*w+b.x*u+c.x*v,a.y*w+b.y*u+c.y*v-1.2f*(u*v+v*w+w*u),a.z*w+b.z*u+c.z*v};
}
static void mesa_sail(t3_object *root,t3_material *material,t3_vec3 a,t3_vec3 b,t3_vec3 c) {
  mb mesh={0};
  for(int i=0;i<8;i++)for(int j=0;j<8-i;j++)for(int half=0;half<(i+j<7?2:1);half++) {
    t3_vec3 q[3]={mesa_sail_point(a,b,c,i/8.f,j/8.f),mesa_sail_point(a,b,c,(i+1)/8.f,j/8.f),mesa_sail_point(a,b,c,i/8.f,(j+1)/8.f)};
    if(half){q[0]=q[1];q[1]=mesa_sail_point(a,b,c,(i+1)/8.f,(j+1)/8.f);}
    t3_vec3 d={q[1].x-q[0].x,q[1].y-q[0].y,q[1].z-q[0].z},e={q[2].x-q[0].x,q[2].y-q[0].y,q[2].z-q[0].z};
    t3_vec3 n={d.y*e.z-d.z*e.y,d.z*e.x-d.x*e.z,d.x*e.y-d.y*e.x};
    float len=sqrtf(n.x*n.x+n.y*n.y+n.z*n.z);n.x/=len;n.y/=len;n.z/=len;
    for(int face=0;face<2;face++) {
      int first=mb_v(&mesh,q[0],n,0,0,1);mb_v(&mesh,q[1],n,1,0,1);mb_v(&mesh,q[2],n,0,1,1);
      mb_t(&mesh,first,first+(face?2:1),first+(face?1:2));n=(t3_vec3){-n.x,-n.y,-n.z};
    }
  }
  t3_geometry *geo=mb_done(&mesh);prop(root,geo,material,0,0,0,1,1,1);t3_release(geo);
}
static void mesa_buildings(t3_object *root,const whimsy_parts *p) {
  mesa_sign_cache_clear();
  const float places[]={mesa_layout.heatsync,mesa_layout.oro,mesa_layout.chupacabra,
    mesa_layout.que_chevere,mesa_layout.phantom_fox,mesa_layout.az_distilling,
    mesa_layout.main_burger,mesa_layout.organic_bean,mesa_layout.bri};
  t3_material *stucco=mat(TEX_CONCRETE,(float[3]){0.95f,0.88f,0.72f},1,1,0.25f,0.9f);
  t3_material *brick=mesa_brick();
  for(int venue=0;venue<9;venue++) {
    float x=places[venue],z=-14.5f,y=bank_y(x,z,2),w=venue==0?13:(venue==4?15:11);
    int first=root->child_count,round_sign_first=-1;
    prop(root,p->box,(venue==2||venue==8)?brick:stucco,x,y+2.1f,z,w,4.2f,5);
    prop(root,p->box,p->cream,x,y+4.25f,z,w+0.3f,0.2f,5.3f);
    prop(root,p->box,p->glass,x,y+1.6f,z+2.53f,w-1,2.8f,0.08f);
    prop(root,p->box,p->black,x,y+1.4f,z+2.6f,1.3f,2.8f,0.08f);
    for(int j=-2;j<=2;j++) prop(root,p->box,p->cream,x+j*2.2f,y+1.6f,z+2.62f,0.12f,3,0.12f);
    /* Layered window displays: recessed dark glazing, warm interior ceiling
     * strips and shelves. These stay in the static city batches. */
    for(int bay=-2;bay<=2;bay++) {
      if(bay==0)continue;
      float bx=x+bay*2.2f;
      prop(root,p->box,p->black,bx,y+1.55f,z+2.59f,1.9f,2.65f,.035f);
      prop(root,p->box,p->brown,bx,y+.68f,z+2.62f,1.65f,.10f,.07f);
      prop(root,p->box,p->lamp,bx,y+2.65f,z+2.62f,1.4f,.035f,.04f);
      for(int item=0;item<3;item++) {
        float xx=bx-.5f+item*.5f;
        prop(root,p->cyl,(venue==0)?p->blue:p->gold,xx,y+.87f,z+2.65f,.06f,.28f,.06f);
      }
    }
    /* Roof coping and rooftop equipment give the silhouettes depth. */
    prop(root,p->box,p->cream,x,y+4.42f,z-2.4f,w,.32f,.20f);
    prop(root,p->box,p->black,x+2,y+4.55f,z-1,.95f,.6f,.8f);
    prop(root,p->box,p->cream,x+2,y+4.88f,z-1,1.05f,.06f,.9f);
    for(int vent=0;vent<4;vent++)
      prop(root,p->box,p->black,x+1.65f+vent*.23f,y+4.92f,z-1,.08f,.025f,.7f);
    if(venue==0) {
      /* HeatSync: stepped white parapet, timber pergola and substantial columns. */
      prop(root,p->box,p->cream,x-1,y+4.9f,z,9,1.3f,5);
      prop(root,p->box,p->cream,x-2,y+5.7f,z,6,0.5f,5);
      prop(root,p->box,p->cream,x,y+3.5f,z+3.3f,w+0.5f,0.3f,1.9f);
      for(int j=-2;j<=2;j++) {
        float cx=x+j*2.8f;
        prop(root,p->cyl,p->cream,cx,y+1.65f,z+3.8f,0.26f,3.3f,0.26f);
        prop(root,p->box,p->cream,cx,y+3.25f,z+3.8f,0.8f,0.25f,0.8f);
        prop(root,p->box,p->cream,cx,y+0.1f,z+3.8f,0.7f,0.2f,0.7f);
      }
      for(int j=-6;j<=6;j++)prop(root,p->box,p->brown,x+j,y+3.8f,z+3.2f,0.1f,0.16f,2.1f);
      prop(root,p->box,p->cream,x-0.8f,y+4.7f,z+2.65f,9,1,0.2f);
      mesa_logo(root,"signs/heatsync.png",x-4.5f,y+4.7f,z+2.78f,0.9f,0.9f);
      mesa_name(root,"HeatSync Labs",x-0.2f,y+4.7f,z+2.79f,7,0.9f);
    } else if(venue==1) {
      /* Oro: stepped adobe facade and its red marquee arrow. */
      prop(root,p->box,p->cream,x,y+4.9f,z,9,1.3f,5);
      mesa_logo(root,"signs/oro.png",x-1,y+4.7f,z+2.7f,3.3f,3.3f);
      mesa_name(root,"ORO BREWERY & TAPROOM",x,y+3.3f,z+2.7f,8.5f,0.65f);
      prop(root,p->box,p->red,x+2.7f,y+4.8f,z+2.8f,0.4f,2.4f,0.25f);
      prop(root,p->box,p->red,x+2.1f,y+3.7f,z+2.8f,1.5f,0.4f,0.25f);
      for(int s=-1;s<=1;s+=2) {
        t3_mesh *arrow=prop(root,p->box,p->red,x+1.4f,y+3.7f+s*0.22f,z+2.8f,0.75f,0.25f,0.25f);
        t3_object_set_rotation(&arrow->base,0,0,-s*0.65f);
      }
      for(int j=0;j<7;j++)prop(root,p->ball,p->lamp,x+2.7f,y+3.9f+j*0.3f,z+2.96f,0.06f,0.06f,0.04f);
    } else if(venue==2) {
      /* Chupacabra: brick, white window frames, round logo and a patio. */
      /* The wall board is painted rust/cream; only the round blade is neon. */
      prop(root,p->box,p->brown,x-1,y+3.65f,z+2.7f,7,0.8f,0.15f);
      mesa_name(root,"CHUPACABRA TAPROOM",x-1,y+3.65f,z+2.8f,6.8f,0.65f);
      round_sign_first=root->child_count;
      t3_geometry *round_back=t3_cylinder_geometry_new(1,1,1,48,1,false);
      t3_mesh *disc=prop(root,round_back,p->black,x+3.8f,y+4.5f,z+2.7f,1.2f,0.2f,1.2f);
      t3_release(round_back);
      t3_object_set_rotation(&disc->base,1.5707963f,0,0);
      mesa_logo(root,"signs/chupacabra.png",x+3.8f,y+4.5f,z+3.1f,2.35f,2.3f);
      t3_material *neon_ring=t3_mesh_basic_material_new(0xffffff);
      neon_ring->color=(t3_color){4,.36f,1.61f};
      t3_geometry *tube=t3_torus_geometry_new(1.14f,.025f,6,48,6.2831853f);
      prop(root,tube,neon_ring,x+3.8f,y+4.5f,z+3.13f,1,1,1);
      t3_release(tube);t3_release(neon_ring);
    }
    if(venue==3) {
      /* Que Chevere: tall white blade sign, terracotta arcade and green doors. */
      prop(root,p->box,p->cream,x-1,y+5.1f,z,7,2.4f,5);
      mesa_logo(root,"signs/quechevere.png",x-1,y+5.2f,z+2.68f,5.8f,1.8f);
      for(int j=-2;j<=2;j++) {
        prop(root,p->box,p->cream,x+j*2.2f,y+1.5f,z+3.2f,.35f,3,1);
        prop(root,p->box,p->green,x+j*2.2f+.8f,y+1.3f,z+2.68f,1.15f,2.6f,.1f);
      }
      for(int j=0;j<40;j++) {
        t3_mesh *tile=prop(root,p->cyl,p->brown,x-5.5f+j*.28f,y+3.25f,z+3,.17f,1.9f,.17f);
        t3_object_set_rotation(&tile->base,1.36f,0,0);
      }
    }
    if(venue==4) {
      /* Phantom Fox: charcoal portal, ochre inset, copper patio screens. */
      prop(root,p->box,p->black,x,y+4.2f,z+2.8f,15.4f,2.2f,.5f);
      for(int j=-1;j<=1;j++)prop(root,p->box,p->black,x+j*6.6f,y+1.6f,z+2.8f,.65f,3.2f,.5f);
      mesa_logo(root,"signs/phantomfox.png",x,y+4.35f,z+3.07f,8.8f,3.9f);
      /* Added side bays widen the frontage without scaling either sign. */
      for(int side=-1;side<=1;side+=2) {
        prop(root,p->box,p->black,x+side*5.55f,y+1.6f,z+2.65f,1.8f,2.8f,.1f);
        prop(root,p->box,p->brown,x+side*5.55f,y+.65f,z+2.72f,1.65f,.12f,.08f);
      }
      /* The official Phantom Fox artwork already includes its wordmark. */
      prop(root,p->box,p->brown,x,y+.65f,z+6.6f,14.5f,1.1f,.08f);
      prop(root,p->box,p->black,x,y+1.25f,z+6.6f,14.7f,.08f,.1f);
      /* Goat & Ram shares this venue; white artwork on a dark patio sign. */
      prop(root,p->box,p->black,x,y+.75f,z+6.68f,4.4f,1.3f,.08f);
      mesa_logo(root,"signs/goatandram.png",x,y+.75f,z+6.74f,4.1f,1.12f);
      /* Charcoal arch spandrels below the broad fascia. */
      mb arches={0};
      for(int bay=-1;bay<=1;bay+=2)for(int j=0;j<16;j++) {
        float u0=j/16.f,u1=(j+1)/16.f,center=x+bay*3.3f;
        float xa=center+(u0-.5f)*5.95f,xb=center+(u1-.5f)*5.95f;
        float ya=y+2.65f+.45f*sqrtf(fmaxf(0,1-(u0*2-1)*(u0*2-1)));
        float yb=y+2.65f+.45f*sqrtf(fmaxf(0,1-(u1*2-1)*(u1*2-1)));
        int v=mb_v(&arches,(t3_vec3){xa,ya,z+3.06f},(t3_vec3){0,0,1},0,0,1);
        mb_v(&arches,(t3_vec3){xb,yb,z+3.06f},(t3_vec3){0,0,1},1,0,1);
        mb_v(&arches,(t3_vec3){xb,y+3.12f,z+3.06f},(t3_vec3){0,0,1},1,1,1);
        mb_v(&arches,(t3_vec3){xa,y+3.12f,z+3.06f},(t3_vec3){0,0,1},0,1,1);
        mb_t(&arches,v,v+1,v+2);mb_t(&arches,v,v+2,v+3);
      }
      t3_geometry *arch=mb_done(&arches);prop(root,arch,p->black,0,0,0,1,1,1);t3_release(arch);
      mesa_sail(root,p->gold,(t3_vec3){x-7,y+3.9f,z+6.2f},(t3_vec3){x-7,y+3.3f,z+3.1f},(t3_vec3){x+3.3f,y+3.25f,z+4.8f});
      mesa_sail(root,p->gold,(t3_vec3){x+1,y+3.35f,z+3.1f},(t3_vec3){x+7,y+3.5f,z+3.1f},(t3_vec3){x+7,y+3.9f,z+6.3f});
      for(int side=-1;side<=1;side+=2) {
        prop(root,p->cyl,p->black,x+side*7,y+1.95f,z+6.25f,.045f,3.9f,.045f);
        float tx=x+side*3.5f;
        prop(root,p->box,p->brown,tx,y+1.1f,z+4.8f,2.4f,.12f,.8f);
        for(int seat=-1;seat<=1;seat+=2) {
          prop(root,p->box,p->brown,tx,y+.58f,z+4.8f+seat*.65f,2.4f,.1f,.35f);
          for(int leg=-1;leg<=1;leg+=2)prop(root,p->box,p->black,tx+leg*.85f,y+.3f,z+4.8f+seat*.65f,.07f,.6f,.07f);
        }
        for(int leg=-1;leg<=1;leg+=2)prop(root,p->box,p->black,tx+leg*.85f,y+.55f,z+4.8f,.09f,1.1f,.5f);
      }
    }
    if(venue==5) {
      /* Arizona Distilling: long navy fascia and recessed shaded glazing. */
      prop(root,p->box,p->black,x,y+4.4f,z+2.7f,11.5f,2,.35f);
      mesa_logo(root,"signs/azdistilling.png",x,y+4.4f,z+2.91f,6.8f,.62f);
      prop(root,p->box,p->cream,x,y+2.85f,z+3.05f,11.8f,.7f,1.1f);
      for(int j=-2;j<=2;j++)prop(root,p->box,p->black,x+j*2.2f,y+1.25f,z+2.7f,1.95f,2.5f,.1f);
      for(int j=-2;j<=2;j+=2) {
        prop(root,p->cyl,p->brown,x+j*2.4f,y+.55f,z+4,.48f,1.1f,.48f);
        prop(root,p->cyl,p->black,x+j*2.4f,y+.3f,z+4,.49f,.045f,.49f);
        prop(root,p->cyl,p->black,x+j*2.4f,y+.85f,z+4,.49f,.045f,.49f);
      }
    }
    if(venue==6) {
      /* Main Burgers: curved cream parapet with dark trim and gold letters. */
      prop(root,p->ball,p->black,x,y+4.7f,z+2.55f,3.4f,1.35f,.25f);
      prop(root,p->ball,p->cream,x,y+4.68f,z+2.92f,3.2f,1.17f,.15f);
      prop(root,p->box,p->cream,x,y+4.35f,z+2.72f,7,1.1f,.2f);
      mesa_name(root,"MAIN",x,y+5.15f,z+3.2f,3,.65f);
      mesa_name(root,"BURGERS",x,y+4.55f,z+3.2f,6.6f,.75f);
      prop(root,p->box,p->black,x,y+3.35f,z+2.9f,11.5f,.45f,1.2f);
      mesa_logo(root,"signs/mainburger.png",x,y+3.37f,z+3.52f,5,.68f);
      for(int j=0;j<3;j++) {
        float tx=x-3.5f+j*3.5f;
        prop(root,p->box,p->black,tx,y+.9f,z+4.5f,1.6f,.12f,1.3f);
        prop(root,p->cyl,p->black,tx,y+.45f,z+4.5f,.06f,.9f,.06f);
      }
      prop(root,p->box,p->black,x,y+.6f,z+6,10.5f,1.2f,.15f);
    }
    if(venue==7) {
      /* Organic Bean: small OBC door seal, greenery, salvaged timber patio. */
      mesa_name(root,"ORGANIC BEAN CAFE",x,y+4.1f,z+2.7f,9,.7f);
      prop(root,p->ring,p->cream,x,y+2.2f,z+2.8f,.38f,.38f,.3f);
      mesa_name(root,"OBC",x,y+2.2f,z+3.1f,.6f,.25f);
      for(int j=0;j<4;j++) {
        prop(root,p->box,p->brown,x,y+.25f+j*.24f,z+6,9.5f,.12f,.12f);
        prop(root,p->box,p->brown,x-4.7f,y+.25f+j*.24f,z+4.6f,.12f,.12f,2.8f);
      }
      for(int side=-1;side<=1;side+=2) {
        /* The foliage frames the whole fascia instead of covering its ends. */
        prop(root,p->cyl,p->brown,x+side*6.3f,y+1.55f,z+3.8f,.12f,3.1f,.12f);
        prop(root,p->box,p->brown,x+side*4.7f,y+1.9f,z+6,.1f,3.8f,.1f);
        for(int j=0;j<6;j++) {
          float a=j*1.0472f;
          prop(root,p->ball,p->green,x+side*6.3f+cw_cos(a)*.55f,y+2.9f+(j%2)*.3f,z+3.8f+cw_sin(a)*.5f,.7f,.65f,.65f);
        }
      }
      prop(root,p->box,p->brown,x,y+3.8f,z+6,9.5f,.1f,.1f);
      for(int j=0;j<12;j++)prop(root,p->ball,p->lamp,x-4.4f+j*.8f,y+3.65f-.3f*cw_sin(j*.2856f),z+6,.055f,.075f,.055f);
      prop(root,p->box,p->green,x,y+.55f,z+4.8f,2.5f,.14f,.65f);
      prop(root,p->box,p->green,x,y+.95f,z+4.5f,2.5f,.7f,.1f);
    }
    if(venue==8) {
      /* B.R.I.: red brick, green lettering and an arcade cabinet roundel. */
      prop(root,p->box,brick,x,y+4.5f,z,11.3f,2.1f,5);
      /* Keep the lettering above the cream cornice, on a contrasting fascia. */
      prop(root,p->box,p->black,x-.6f,y+4.95f,z+2.7f,9.7f,1.12f,.12f);
      mesa_name(root,"B.R.I. TAPROOM & ARCADE",x-.6f,y+4.95f,z+2.79f,9.4f,1.05f);
      t3_mesh *round=prop(root,p->cyl,p->black,x+4.7f,y+5.05f,z+2.7f,.8f,.22f,.8f);
      t3_object_set_rotation(&round->base,1.5707963f,0,0);
      prop(root,p->ring,p->cream,x+4.7f,y+5.05f,z+3.05f,.72f,.72f,.3f);
      prop(root,p->box,p->green,x+4.7f,y+5.05f,z+3.08f,.43f,.85f,.05f);
      prop(root,p->box,p->black,x+4.7f,y+5.2f,z+3.12f,.29f,.32f,.04f);
      for(int j=0;j<4;j++) {
        float ax=x-4+j*2.6f;
        prop(root,p->box,p->black,ax,y+1.1f,z+2.7f,1.1f,1.8f,.15f);
        prop(root,p->box,j%2?p->blue:p->pink,ax,y+1.5f,z+2.8f,.8f,.65f,.05f);
        prop(root,p->box,p->lamp,ax,y+2.1f,z+2.81f,.95f,.18f,.04f);
      }
    }
    if(venue>0 && venue<5) {
      float px=x-3,pz=z+5;
      prop(root,p->box,p->brown,px,y+0.1f,pz,5,0.2f,3.4f);
      for(int j=0;j<6;j++)prop(root,p->box,p->black,px-2.4f+j*0.96f,y+0.65f,pz+1.6f,0.07f,1.3f,0.07f);
      prop(root,p->box,p->black,px,y+1.15f,pz+1.6f,5,0.08f,0.08f);
      for(int j=0;j<(venue==4?0:2);j++) {
        float tx=px-1.3f+j*2.6f;
        prop(root,p->cyl,p->black,tx,y+0.65f,pz,0.07f,1.1f,0.07f);
        prop(root,p->cyl,p->brown,tx,y+1.2f,pz,0.8f,0.12f,0.8f);
        if(venue!=2) {
          prop(root,p->cyl,p->black,tx,y+2,pz,0.035f,2.8f,0.035f);
          prop(root,p->cone,venue==1?p->red:p->gold,tx,y+3.1f,pz,1.4f,0.4f,1.4f);
        }
      }
      if(venue==2) {
        /* Reference patio: overlapping ochre/cream tension sails, not parasols.
         * Keep the high corners below the wall lettering and circular neon. */
        const float left=px-2.6f,right=px+2.6f,front=pz+1.5f,back=z+3.1f;
        mesa_sail(root,p->gold,(t3_vec3){left,y+3.35f,front},
          (t3_vec3){left,y+3.1f,back},(t3_vec3){right,y+3.25f,back});
        mesa_sail(root,p->cream,(t3_vec3){left+.65f,y+3.25f,front},
          (t3_vec3){right,y+3.35f,front},(t3_vec3){right,y+3.15f,back});
        for(int side=0;side<2;side++) {
          float sx=side?right:left;
          prop(root,p->cyl,p->black,sx,y+1.675f,front,.045f,3.35f,.045f);
          prop(root,p->cyl,p->black,sx,y+1.575f,back,.045f,3.15f,.045f);
        }
      }
      for(int j=0;j<10;j++)prop(root,p->ball,p->lamp,x-5+j,y+3.4f-0.3f*cw_sin(j*0.349f),z+3.2f,0.07f,0.1f,0.07f);
    }
    /* A broader street frontage, with gaps retained between landmarks. */
    for(int k=first;k<root->child_count;k++) {
      t3_object *o=root->children[k];
      o->position.x=x+(o->position.x-x)*1.22f;o->scale.x*=1.22f;
      o->position.y=y+(o->position.y-y)*1.08f;o->scale.y*=1.08f;
      if(o->position.z>z+2.6f){o->position.z=z+2.6f+(o->position.z-z-2.6f)*.62f;o->scale.z*=.62f;}
    }
    /* Frontage widening must not turn the iconic circular sign into an oval.
     * The backing cylinder's local Z is vertical after its quarter turn. */
    if(round_sign_first>=0) {
      for(int j=0;j<3;j++)root->children[round_sign_first+j]->scale.x*=1.08f/1.22f;
      root->children[round_sign_first]->scale.z*=1.08f/.62f;
    }
  }
  mesa_tall_buildings(root,p,brick);
  mesa_food_shops(root,p,brick);
  mesa_neighbor_restaurants(root,p);
  mesa_nct(root,p,brick);
  mesa_gus(root,p,brick);
  mesa_diamond_dons(root,p);
  mesa_hopes(root,p);
  mesa_espiritu(root,p);
  mesa_mangos(root,p,brick);
  mesa_margaritas(root,p);
  mesa_lost_dutchman(root,p);
  mesa_casa_copal(root,p);
  mesa_guadalupe(root,p);
  mesa_republica(root,p);
  mesa_pizza_mart(root,p);
  mesa_sign_cache_clear();
  t3_release(stucco);t3_release(brick);
}
