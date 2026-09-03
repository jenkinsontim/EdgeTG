#include "ts_layers.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests=0, passed=0;
#define CHECK(x,msg) do { tests++; if (!(x)) { printf("FAIL: %s\n", msg); return 1; } passed++; } while(0)

static int forest_roundtrip(const TSNode*f,size_t n){
    char*s=NULL,*t=NULL;size_t a=0,b=0;TSNode*g=NULL;size_t gn=0;
    if(ts_encode_forest(f,n,&s,&a)!=TS_OK)return 0;
    if(ts_parse_forest(s,(size_t)-1,&g,&gn)!=TS_OK||gn!=n){free(s);ts_free_forest(g,gn);return 0;}
    int ok=ts_encode_forest(g,gn,&t,&b)==TS_OK&&a==b&&memcmp(s,t,a)==0;
    free(s);free(t);ts_free_forest(g,gn);return ok;
}

static TSNode *random_tree(unsigned depth){
    TSNode *n=(TSNode*)calloc(1,sizeof(*n));if(!n)return NULL;
    if(depth==0 || rand()%3) return n;
    size_t k=(size_t)(1+rand()%3);n->child_count=k;n->children=(TSNode*)calloc(k,sizeof(*n->children));if(!n->children){free(n);return NULL;}
    for(size_t i=0;i<k;i++){TSNode*c=random_tree(depth-1);if(!c){ts_free_tree(n);free(n);return NULL;}n->children[i]=*c;free(c);}return n;
}

static int value_equal(const TSValue*a,const TSValue*b,size_t n){for(size_t i=0;i<n;i++)if(a[i].len!=b[i].len||memcmp(a[i].data,b[i].data,a[i].len)!=0)return 0;return 1;}

int main(void){ setbuf(stdout,NULL);
    printf("EdgeTG â€” FOUR OPTIONAL LAYERS / C VERIFICATION\n");
    printf("Canonical alphabet: _ / \\\n");
    printf("Core grammar is untouched: no values, names, types, or extra glyphs.\n\n");

    TSNode *tree=NULL;CHECK(ts_parse("_/__\\",64,&tree)==TS_OK,"core parse");CHECK(ts_canonical_roundtrip(tree),"core roundtrip");
    TSNode *bad=NULL;CHECK(ts_parse("_/\\",64,&bad)==TS_ERR_EMPTY_CHILDREN,"empty child list remains illegal");

    printf("LAYER 1 â€” paired data side-channel\n");
    TSValue vals[3]={{(const uint8_t*)"ROOT",4},{(const uint8_t*)"LEFT",4},{(const uint8_t*)"RIGHT",5}};
    TSPaired p={0};CHECK(ts_pair_attach(tree,vals,3,&p)==TS_OK,"attach");CHECK(strcmp(p.topology,"_/__\\")==0,"topology unchanged");CHECK(p.node_count==3&&memcmp(p.values[2].data,"RIGHT",5)==0,"preorder index pairing");
    char *top=NULL;TSValue *v2=NULL;size_t vc=0;CHECK(ts_pair_encode(tree,vals,3,&top,&v2,&vc)==TS_OK,"paired encode");TSNode*dt=NULL;TSPaired dp={0};CHECK(ts_pair_decode(top,v2,vc,64,&dt,&dp)==TS_OK,"paired decode");CHECK(strcmp(top,"_/__\\")==0&&value_equal(v2,vals,3)&&ts_canonical_roundtrip(dt),"paired roundtrip");
    uint8_t *vb=NULL;size_t vbl=0;TSValue *vd=NULL;size_t vdc=0;CHECK(ts_values_encode(vals,3,&vb,&vbl)==TS_OK,"value serialization");CHECK(ts_values_decode(vb,vbl,&vd,&vdc)==TS_OK&&vdc==3&&value_equal(vals,vd,3),"value serialization roundtrip");ts_values_free(vd,vdc);
    free(top);free(v2);free(vb);ts_pair_free(&p);ts_pair_free(&dp);ts_free_tree(dt);free(dt);
    printf("  PASS: topology stays pure; values are a separate preorder-indexed artifact.\n\n");

    printf("LAYER 2 â€” schema / arity constraints\n");
    TSArityRule rr[]={{0,2,2}};TSSchema good={64,0,2,rr,1};TSSchemaResult sr={0};CHECK(ts_schema_validate(tree,&good,&sr),"schema pass");
    TSSchema bad_arity={64,0,1,NULL,0};CHECK(!ts_schema_validate(tree,&bad_arity,&sr)&&sr.code==TS_SCHEMA_MAX_CHILDREN&&sr.actual_children==2,"schema arity fail");
    TSSchema bad_depth={1,-1,-1,NULL,0};CHECK(!ts_schema_validate(tree,&bad_depth,&sr)&&sr.code==TS_SCHEMA_MAX_DEPTH,"schema depth fail");
    printf("  PASS: validation is external and reports a specific reason/index.\n\n");

    printf("LAYER 3 â€” forest operators\n");
    TSNode *fa=NULL,*fb=NULL;size_t fan=0,fbn=0;CHECK(ts_parse_forest("__",64,&fa,&fan)==TS_OK,"forest A");CHECK(ts_parse_forest("_/__\\",64,&fb,&fbn)==TS_OK,"forest B");
    TSNode *fc=NULL;size_t fcn=0;CHECK(ts_forest_concat(fa,fan,fb,fbn,&fc,&fcn)==TS_OK&&fcn==3&&forest_roundtrip(fc,fcn),"concat roundtrip");ts_free_forest(fc,fcn);
    TSNode *fg=NULL;CHECK(ts_forest_graft(fa,fan,0,NULL,0,&fb[0],64,&fg)==TS_OK&&forest_roundtrip(fg,fan),"graft roundtrip");ts_free_forest(fg,fan);
    TSNode *fm=NULL;size_t fmn=0;CHECK(ts_forest_pair(fa,fan,fb,fbn,64,&fm,&fmn)!=TS_OK,"pair mismatch fails loudly");
    TSNode *fa1=NULL,*fb1=NULL;size_t fa1n=0,fb1n=0;CHECK(ts_parse_forest("_",64,&fa1,&fa1n)==TS_OK,"pair A");CHECK(ts_parse_forest("_/__\\",64,&fb1,&fb1n)==TS_OK,"pair B");CHECK(ts_forest_pair(fa1,fa1n,fb1,fb1n,64,&fm,&fmn)==TS_OK&&fmn==1&&forest_roundtrip(fm,fmn),"pair roundtrip");ts_free_forest(fm,fmn);ts_free_forest(fa1,fa1n);ts_free_forest(fb1,fb1n);ts_free_forest(fa,fan);ts_free_forest(fb,fbn);
    /* Explicit graft depth guard. */
    TSNode *gsrc=NULL,*gdst=NULL,*gforest=NULL;CHECK(ts_parse("_/_\\",64,&gsrc)==TS_OK&&ts_parse("_/_\\",64,&gdst)==TS_OK,"graft inputs");gforest=gdst;CHECK(ts_forest_graft(gforest,1,0,NULL,0,gsrc,2,&fg)==TS_ERR_DEPTH,"graft depth fails before commit");ts_free_tree(gsrc);free(gsrc);ts_free_tree(gdst);free(gdst);
    printf("  PASS: concat, graft, and pairing produce parseable canonical forests; invalid cases fail.\n\n");

    printf("LAYER 4 â€” explicit unordered normalization\n");
    TSNode *u=NULL,*n1=NULL,*n2=NULL;CHECK(ts_parse("_/_/_\\_\\",64,&u)==TS_OK,"unordered input");CHECK(ts_unordered_normalize(u,&n1)==TS_OK&&ts_canonical_roundtrip(n1),"normalize");CHECK(ts_unordered_normalize(n1,&n2)==TS_OK,"normalize again");char*s1=NULL,*s2=NULL;ts_encode(n1,&s1,NULL);ts_encode(n2,&s2,NULL);CHECK(strcmp(s1,s2)==0,"idempotence");free(s1);free(s2);ts_free_tree(u);free(u);ts_free_tree(n1);free(n1);ts_free_tree(n2);free(n2);
    /* Paired normalization must carry the same child permutation into values. */
    TSNode *up=NULL,*upn=NULL;CHECK(ts_parse("_/_/_\\_\\",64,&up)==TS_OK,"paired unordered input");TSValue uv[4]={{(const uint8_t*)"R",1},{(const uint8_t*)"U",1},{(const uint8_t*)"u-leaf",6},{(const uint8_t*)"L",1}};TSValue *uvn=NULL;CHECK(ts_unordered_normalize_paired(up,uv,4,&upn,&uvn)==TS_OK&&ts_canonical_roundtrip(upn),"paired normalize");CHECK(uvn[0].data==uv[0].data&&uvn[1].data==uv[3].data&&uvn[2].data==uv[1].data&&uvn[3].data==uv[2].data,"paired permutation follows topology");free(uvn);ts_free_tree(up);free(up);ts_free_tree(upn);free(upn);
    printf("  PASS: normalization is opt-in, deterministic, idempotent; paired values follow subtree permutation.\n\n");

    printf("FUZZ-STYLE INVARIANT CHECK\n");
    srand(20260831);size_t cases=0;
    for(size_t i=0;i<250;i++){
        TSNode*t=random_tree(5);CHECK(t!=NULL,"random tree allocation");cases++;
        CHECK(ts_canonical_roundtrip(t),"fuzz base roundtrip");
        TSNode*nu=NULL;CHECK(ts_unordered_normalize(t,&nu)==TS_OK&&ts_canonical_roundtrip(nu),"fuzz normalize roundtrip");
        TSNode*ff=NULL;size_t fn=0;CHECK(ts_forest_concat(t,1,t,1,&ff,&fn)==TS_OK&&forest_roundtrip(ff,fn),"fuzz concat roundtrip");ts_free_forest(ff,fn);
        TSNode*fp=NULL;size_t fpn=0;CHECK(ts_forest_pair(t,1,t,1,128,&fp,&fpn)==TS_OK&&forest_roundtrip(fp,fpn),"fuzz pair roundtrip");ts_free_forest(fp,fpn);
        TSNode*fg2=NULL;CHECK(ts_forest_graft(t,1,0,NULL,0,t,128,&fg2)==TS_OK&&forest_roundtrip(fg2,1),"fuzz graft roundtrip");ts_free_forest(fg2,1);
        ts_free_tree(nu);free(nu);ts_free_tree(t);free(t);
    }
    printf("  PASS: %zu generated trees exercised topology-producing layer operations.\n\n",cases);

    ts_free_tree(tree); free(tree);

    printf("RESULT: %d/%d tests passed, 0 failed.\n",passed,tests);
    return 0;
}
