#include "ts_layers.h"
#include <stdlib.h>
#include <string.h>

static TSError clone_into(TSNode *dst, const TSNode *src) {
    TSNode *tmp = NULL;
    TSError e = ts_clone(src, &tmp);
    if (e != TS_OK) return e;
    *dst = *tmp;
    free(tmp);
    return TS_OK;
}

/* ---------- Layer 1 ---------- */
TSError ts_pair_attach(const TSNode *tree, const TSValue *values, size_t value_count, TSPaired *out) {
    if (!tree || !values || !out) return TS_ERR_INVALID_ARG;
    size_t n = ts_count_nodes(tree);
    if (value_count != n) return TS_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    TSError e = ts_encode(tree, &out->topology, NULL);
    if (e != TS_OK) return e;
    out->values = (TSValue *)malloc(n * sizeof(*out->values));
    if (!out->values) { free(out->topology); memset(out, 0, sizeof(*out)); return TS_ERR_OOM; }
    memcpy(out->values, values, n * sizeof(*values));
    out->node_count = n;
    return TS_OK;
}

TSError ts_pair_detach(const TSNode *tree, const TSPaired *paired, TSValue *out_values, size_t value_count) {
    if (!tree || !paired || !out_values) return TS_ERR_INVALID_ARG;
    size_t n = ts_count_nodes(tree);
    if (paired->node_count != n || value_count != n) return TS_ERR_INVALID_ARG;
    memcpy(out_values, paired->values, n * sizeof(*out_values));
    return TS_OK;
}

TSError ts_pair_encode(const TSNode *tree, const TSValue *values, size_t value_count,
                       char **topology_out, TSValue **values_out, size_t *count_out) {
    if (!tree || !values || !topology_out || !values_out) return TS_ERR_INVALID_ARG;
    size_t n = ts_count_nodes(tree);
    if (value_count != n) return TS_ERR_INVALID_ARG;
    TSError e = ts_encode(tree, topology_out, NULL);
    if (e != TS_OK) return e;
    TSValue *v = (TSValue *)malloc(n * sizeof(*v));
    if (!v) { free(*topology_out); *topology_out = NULL; return TS_ERR_OOM; }
    memcpy(v, values, n * sizeof(*v));
    *values_out = v;
    if (count_out) *count_out = n;
    return TS_OK;
}

TSError ts_pair_decode(const char *topology, const TSValue *values, size_t value_count,
                       size_t max_depth, TSNode **tree_out, TSPaired *paired_out) {
    if (!topology || !values || !tree_out) return TS_ERR_INVALID_ARG;
    *tree_out = NULL;
    TSError e = ts_parse(topology, max_depth, tree_out);
    if (e != TS_OK) return e;
    size_t n = ts_count_nodes(*tree_out);
    if (value_count != n) { ts_free_tree(*tree_out); free(*tree_out); *tree_out = NULL; return TS_ERR_INVALID_ARG; }
    if (paired_out) {
        e = ts_pair_attach(*tree_out, values, value_count, paired_out);
        if (e != TS_OK) { ts_free_tree(*tree_out); free(*tree_out); *tree_out = NULL; }
    }
    return e;
}

void ts_pair_free(TSPaired *p) {
    if (!p) return;
    free(p->topology);
    free(p->values);
    memset(p, 0, sizeof(*p));
}

/* Binary value-array side artifact:
 * [u32 little-endian count] then count times [u32 little-endian length][bytes]. */
static void put_u32(uint8_t *p, uint32_t x) {
    p[0]=(uint8_t)x; p[1]=(uint8_t)(x>>8); p[2]=(uint8_t)(x>>16); p[3]=(uint8_t)(x>>24);
}
static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
TSError ts_values_encode(const TSValue *values, size_t count, uint8_t **out, size_t *out_len) {
    if (!values || !out) return TS_ERR_INVALID_ARG;
    if (count > UINT32_MAX) return TS_ERR_INVALID_ARG;
    size_t total = 4;
    for (size_t i=0;i<count;i++) { if (values[i].len > UINT32_MAX || total > SIZE_MAX-4-values[i].len) return TS_ERR_INVALID_ARG; total += 4 + values[i].len; }
    uint8_t *buf=(uint8_t*)malloc(total); if(!buf)return TS_ERR_OOM;
    put_u32(buf,(uint32_t)count); size_t pos=4;
    for(size_t i=0;i<count;i++){
        put_u32(buf+pos,(uint32_t)values[i].len);
        pos+=4;
        if (values[i].len > 0) {
            memcpy(buf+pos,values[i].data,values[i].len);
        }
        pos+=values[i].len;
    }
    *out=buf;if(out_len)*out_len=total;return TS_OK;
}
TSError ts_values_decode(const uint8_t *buf,size_t len,TSValue **out,size_t*out_count){
    if (!buf || !out || len < 4) return TS_ERR_INVALID_ARG;
    uint32_t count=get_u32(buf); size_t pos=4;
    TSValue*v=(TSValue*)calloc(count?count:1,sizeof(*v));if(!v)return TS_ERR_OOM;
    for(uint32_t i=0;i<count;i++){
        if(len-pos<4){free(v);return TS_ERR_INVALID_ARG;}
        uint32_t n=get_u32(buf+pos);pos+=4;
        if(n>len-pos){free(v);return TS_ERR_INVALID_ARG;}
        uint8_t*d=(uint8_t*)malloc(n?n:1);
        if(!d){for(uint32_t j=0;j<i;j++)free((void*)v[j].data);free(v);return TS_ERR_OOM;}
        if (n > 0) {
            memcpy(d,buf+pos,n);
        }
        pos+=n;v[i].data=d;v[i].len=n;
    }
    if(pos!=len){for(uint32_t i=0;i<count;i++)free((void*)v[i].data);free(v);return TS_ERR_INVALID_ARG;}*out=v;if(out_count)*out_count=count;return TS_OK;
}
void ts_values_free(TSValue*values,size_t count){if(!values)return;for(size_t i=0;i<count;i++)free((void*)values[i].data);free(values);}

/* ---------- Layer 2 ---------- */
static const TSArityRule *find_rule(const TSSchema *s,size_t idx){for(size_t i=0;i<s->rule_count;i++)if(s->rules[i].preorder_index==idx)return &s->rules[i];return NULL;}
static bool schema_walk(const TSNode*n,const TSSchema*s,size_t idx,size_t depth,TSSchemaResult*r,size_t*next){
    if(depth>s->max_depth){if(r){r->code=TS_SCHEMA_MAX_DEPTH;r->preorder_index=idx;r->depth=depth;}return false;}
    size_t k=n->child_count;
    if(s->min_children>=0&&(int)k<s->min_children){if(r){r->code=TS_SCHEMA_MIN_CHILDREN;r->preorder_index=idx;r->depth=depth;r->actual_children=k;r->limit=s->min_children;}return false;}
    if(s->max_children>=0&&(int)k>s->max_children){if(r){r->code=TS_SCHEMA_MAX_CHILDREN;r->preorder_index=idx;r->depth=depth;r->actual_children=k;r->limit=s->max_children;}return false;}
    const TSArityRule*rr=find_rule(s,idx);
    if(rr){if(rr->min_children>=0&&(int)k<rr->min_children){if(r){r->code=TS_SCHEMA_ROLE_MIN_CHILDREN;r->preorder_index=idx;r->depth=depth;r->actual_children=k;r->limit=rr->min_children;}return false;}if(rr->max_children>=0&&(int)k>rr->max_children){if(r){r->code=TS_SCHEMA_ROLE_MAX_CHILDREN;r->preorder_index=idx;r->depth=depth;r->actual_children=k;r->limit=rr->max_children;}return false;}}
    (*next)++;for(size_t c=0;c<n->child_count;c++)if(!schema_walk(&n->children[c],s,*next,depth+1,r,next))return false;return true;
}
bool ts_schema_validate(const TSNode*tree,const TSSchema*s,TSSchemaResult*r){if(!tree||!s)return false;if(r)memset(r,0,sizeof(*r));size_t next=0;return schema_walk(tree,s,0,1,r,&next);}

/* ---------- Layer 3 ---------- */
TSError ts_forest_concat(const TSNode*a,size_t an,const TSNode*b,size_t bn,TSNode**out,size_t*out_n){
    if (!out || !out_n || (an && !a) || (bn && !b)) return TS_ERR_INVALID_ARG;
    *out=NULL; *out_n=an+bn; if(!*out_n) return TS_OK;
    TSNode*f=(TSNode*)calloc(*out_n,sizeof(*f));if(!f)return TS_ERR_OOM;
    for(size_t i=0;i<an+bn;i++){const TSNode*src=i<an?&a[i]:&b[i-an];TSError e=clone_into(&f[i],src);if(e){ts_free_forest(f,i);return e;}}
    *out=f;return TS_OK;
}
static TSNode* node_at_path(TSNode*root,const size_t*path,size_t path_len){TSNode*n=root;for(size_t i=0;i<path_len;i++){if(!path||path[i]>=n->child_count)return NULL;n=&n->children[path[i]];}return n;}
TSError ts_forest_graft(const TSNode*forest,size_t forest_n,size_t dst_root,const size_t*path,size_t path_len,const TSNode*source,size_t max_depth,TSNode**out_forest){
    if (!forest || !source || !out_forest || dst_root >= forest_n || (path_len && !path)) return TS_ERR_INVALID_ARG;
    *out_forest=NULL; TSNode*f=NULL; size_t fn=0; TSError e=ts_forest_concat(forest,forest_n,NULL,0,&f,&fn); if(e) return e;
    TSNode*dst=node_at_path(&f[dst_root],path,path_len);if(!dst){ts_free_forest(f,fn);return TS_ERR_INVALID_ARG;}
    size_t resulting_depth=(path_len+1)+ts_depth(source);if(resulting_depth>max_depth){ts_free_forest(f,fn);return TS_ERR_DEPTH;}
    TSNode*src=NULL;e=ts_clone(source,&src);if(e){ts_free_forest(f,fn);return e;}
    TSNode*k=(TSNode*)realloc(dst->children,(dst->child_count+1)*sizeof(*k));if(!k){ts_free_tree(src);free(src);ts_free_forest(f,fn);return TS_ERR_OOM;}dst->children=k;dst->children[dst->child_count++]=*src;free(src);*out_forest=f;return TS_OK;
}
TSError ts_forest_pair(const TSNode*a,size_t an,const TSNode*b,size_t bn,size_t max_depth,TSNode**out,size_t*out_n){
    if (!out || !out_n || (an && !a) || (bn && !b) || an != bn) return TS_ERR_INVALID_ARG;
    *out=NULL; *out_n=an; if(!an) return TS_OK; TSNode*f=(TSNode*)calloc(an,sizeof(*f)); if(!f) return TS_ERR_OOM;
    for(size_t i=0;i<an;i++){if(1+ts_depth(&a[i])>max_depth||1+ts_depth(&b[i])>max_depth){ts_free_forest(f,i);return TS_ERR_DEPTH;}f[i].child_count=2;f[i].children=(TSNode*)calloc(2,sizeof(TSNode));if(!f[i].children){ts_free_forest(f,i);return TS_ERR_OOM;}TSError e=clone_into(&f[i].children[0],&a[i]);if(e){ts_free_forest(f,i+1);return e;}e=clone_into(&f[i].children[1],&b[i]);if(e){ts_free_forest(f,i+1);return e;}if(ts_depth(&f[i])>max_depth){ts_free_forest(f,i+1);return TS_ERR_DEPTH;}}
    *out=f;return TS_OK;
}

/* ---------- Layer 4 ---------- */
static int cmp_nodes(const void*A,const void*B){const TSNode*a=(const TSNode*)A,*b=(const TSNode*)B;char*sa=NULL,*sb=NULL;TSError ea=ts_encode(a,&sa,NULL),eb=ts_encode(b,&sb,NULL);if(ea!=TS_OK||eb!=TS_OK){free(sa);free(sb);return 0;}int c=strcmp(sa,sb);free(sa);free(sb);return c;}
TSError ts_unordered_normalize(const TSNode*tree,TSNode**out){
    if (!tree || !out) return TS_ERR_INVALID_ARG;
    *out=NULL; TSError e=ts_clone(tree,out); if(e) return e; TSNode*n=*out;
    for(size_t i=0;i<n->child_count;i++){TSNode*t=NULL;e=ts_unordered_normalize(&n->children[i],&t);if(e){ts_free_tree(n);free(n);*out=NULL;return e;}ts_free_tree(&n->children[i]);n->children[i]=*t;free(t);}
    if(n->child_count>1) qsort(n->children,n->child_count,sizeof(*n->children),cmp_nodes);
    return TS_OK;
}

/* Paired normalization: recursively normalize each child, then reorder whole child
 * subtrees by canonical string and apply the same permutation to their preorder value blocks. */
typedef struct { TSNode tree; TSValue *values; size_t count; char *key; } PChild;
static void free_pchild(PChild*p){if(!p)return;ts_free_tree(&p->tree);free(p->values);free(p->key);}
static int cmp_pchild(const void*A,const void*B){const PChild*a=(const PChild*)A,*b=(const PChild*)B;return strcmp(a->key,b->key);}
static TSError norm_pair_rec(const TSNode*src,const TSValue*vals,TSNode*out,TSValue*outvals){
    out->child_count=src->child_count;
    out->children=NULL;
    outvals[0]=vals[0];
    if(!src->child_count)return TS_OK;
    out->children=(TSNode*)calloc(src->child_count,sizeof(*out->children));
    if(!out->children)return TS_ERR_OOM;
    PChild*kids=(PChild*)calloc(src->child_count,sizeof(*kids));
    if(!kids){free(out->children);out->children=NULL;return TS_ERR_OOM;}
    size_t off=1;
    for(size_t i=0;i<src->child_count;i++){
        kids[i].count=ts_count_nodes(&src->children[i]);
        TSValue *invals=(TSValue*)malloc(kids[i].count*sizeof(*invals));
        kids[i].values=(TSValue*)malloc(kids[i].count*sizeof(*kids[i].values));
        if(!invals||!kids[i].values){free(invals);free(kids[i].values);for(size_t j=0;j<i;j++)free_pchild(&kids[j]);free(kids);ts_free_tree(out);return TS_ERR_OOM;}
        memcpy(invals,vals+off,kids[i].count*sizeof(*invals));
        TSError e=norm_pair_rec(&src->children[i],invals,&kids[i].tree,kids[i].values);
        free(invals);
        if(e){for(size_t j=0;j<=i;j++)free_pchild(&kids[j]);free(kids);ts_free_tree(out);return e;}
        off+=kids[i].count;
        e=ts_encode(&kids[i].tree,&kids[i].key,NULL);
        if(e){for(size_t j=0;j<=i;j++)free_pchild(&kids[j]);free(kids);ts_free_tree(out);return e;}
    }
    qsort(kids,src->child_count,sizeof(*kids),cmp_pchild);
    off=1;
    for(size_t i=0;i<src->child_count;i++){
        out->children[i]=kids[i].tree;
        kids[i].tree.children=NULL;
        kids[i].tree.child_count=0;
        memcpy(outvals+off,kids[i].values,kids[i].count*sizeof(TSValue));
        off+=kids[i].count;
        free(kids[i].values);
        free(kids[i].key);
    }
    free(kids);
    return TS_OK;
}

TSError ts_unordered_normalize_paired(const TSNode*tree,const TSValue*values,size_t value_count,TSNode**out_tree,TSValue**out_values){
    if (!tree || !values || !out_tree || !out_values) return TS_ERR_INVALID_ARG;
    *out_tree=NULL; *out_values=NULL; size_t n=ts_count_nodes(tree); if(value_count!=n) return TS_ERR_INVALID_ARG;
    TSNode*t=(TSNode*)calloc(1,sizeof(*t));TSValue*v=(TSValue*)malloc(n*sizeof(*v));if(!t||!v){free(t);free(v);return TS_ERR_OOM;}
    TSError e=norm_pair_rec(tree,values,t,v);if(e){ts_free_tree(t);free(t);free(v);return e;}*out_tree=t;*out_values=v;return TS_OK;
}

bool ts_canonical_roundtrip(const TSNode*tree){if(!tree)return false;char*s=NULL,*t=NULL;size_t a=0,b=0;if(ts_encode(tree,&s,&a)!=TS_OK)return false;TSNode*p=NULL;bool ok=ts_parse(s,(size_t)-1,&p)==TS_OK;if(ok)ok=ts_encode(p,&t,&b)==TS_OK&&a==b&&memcmp(s,t,a)==0;free(s);free(t);if(p){ts_free_tree(p);free(p);}return ok;}
