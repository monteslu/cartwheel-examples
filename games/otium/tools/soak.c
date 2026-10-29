#include <stdio.h>
#include "game.h"
int main(int argc,char**argv){
  int games = argc>1?atoi(argv[1]):20; int wins[NP]={0}; double tot[NP]={0};
  int gs[GODS]={0};
  for(int s=1;s<=games;s++){
    sim S; sim_start(&S,(uint32_t)s*7919u);
    int pers[NP]={ (s)%3, (s+1)%3, (s+2)%3 };
    while(!S.over){
      for(int p=0;p<NP;p++) if(sim_idle(&S,p)){ order_req r=ai_choose(&S,p,pers[p]); if(r.order==O_NONE||!sim_order(&S,p,&r)) sim_wait(&S,p,1);}
      sim_clear_events(&S); sim_tick(&S);
    }
    int b=0; for(int p=0;p<NP;p++){ int t=sim_total(&S,p); tot[pers[p]]+=t; if(t>sim_total(&S,b)) b=p;}
    wins[pers[b]]++;
    if(s<=3){ for(int p=0;p<NP;p++){ printf("g%d p%d pers%d total %d houses %d units %d coins %d orders %d fav",s,p,pers[p],sim_total(&S,p),S.p[p].houses,S.p[p].n_units,S.p[p].coins,S.orders_done[p]); for(int g=0;g<GODS;g++)printf(" %d*%d",S.p[p].favor[g],sim_score(&S,p,g)); printf("\n");}}
  }
  for(int k=0;k<NP;k++) printf("pers %d wins %d avg %.1f\n",k,wins[k],tot[k]/games);
}
