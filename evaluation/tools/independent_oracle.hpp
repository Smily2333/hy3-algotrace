#pragma once
#include <sstream>
#include <climits>
#include <numeric>
#include <functional>
// Independent exponential oracles, only for the small fixed tests (not candidates).
inline long long oracle(const std::string& problem,const std::string& input){
 std::istringstream in(input);int n;in>>n;if(!in||n<0||n>10)throw std::runtime_error("oracle small-case limit");
 std::vector<long long>a(n),b(n);long long B=0,F=0;
 if(problem=="p07")in>>B>>F;else if(problem=="p08")in>>B;
 for(int i=0;i<n;++i){in>>a[i];if(problem=="p03"||problem=="p06"||problem=="p07")in>>b[i];}
 if(!in)throw std::runtime_error("oracle input");
 long long best=LLONG_MAX;
 if(problem=="p01"){
    long long sum=std::accumulate(a.begin(),a.end(),0LL);
    for(int mask=0;mask<(1<<n);++mask){long long s=0,k=0;for(int i=0;i<n;++i)if(mask>>i&1){s+=a[i];++k;}if(2*s>sum)best=std::min(best,k);}
 }else if(problem=="p02"){
    std::vector<int>p(n);std::iota(p.begin(),p.end(),0);
    do{long long t=0,cost=0;for(int i:p){cost+=t;t+=a[i];}best=std::min(best,cost);}while(std::next_permutation(p.begin(),p.end()));
 }else if(problem=="p03"){
    best=0;for(int mask=0;mask<(1<<n);++mask){bool ok=true;int k=0;for(int i=0;i<n;++i)if(mask>>i&1){++k;for(int j=i+1;j<n;++j)if((mask>>j&1)&&std::max(a[i],a[j])<std::min(b[i],b[j]))ok=false;}if(ok)best=std::max(best,(long long)k);}
 }else if(problem=="p04"){
    std::function<long long(std::vector<long long>)> f=[&](std::vector<long long>x)->long long{
        if(x.size()<2)return 0;
        long long ans=LLONG_MAX;
        for(std::size_t i=0;i<x.size();++i)for(std::size_t j=i+1;j<x.size();++j){auto y=x;auto s=x[i]+x[j];y.erase(y.begin()+j);y.erase(y.begin()+i);y.push_back(s);ans=std::min(ans,s+f(y));}
        return ans;};
    best=f(a);
 }else if(problem=="p05"){
    long long cap=*std::max_element(a.begin(),a.end())+n;if(cap>100)throw std::runtime_error("oracle cap");
    std::vector<long long>dp(cap+1,LLONG_MAX/4);
    for(long long v=a[0];v<=cap;++v)dp[v]=v-a[0];
    for(int i=1;i<n;++i){std::vector<long long>next(cap+1,LLONG_MAX/4);for(long long v=a[i];v<=cap;++v)for(long long u=0;u<v;++u)next[v]=std::min(next[v],dp[u]+v-a[i]);dp=next;}
    best=*std::min_element(dp.begin(),dp.end());
 }else if(problem=="p06"){
    best=0;for(int mask=0;mask<(1<<n);++mask){std::vector<int>p;for(int i=0;i<n;++i)if(mask>>i&1)p.push_back(i);
        do{long long t=0;bool ok=true;for(int i:p){t+=a[i];ok=ok&&t<=b[i];}if(ok)best=std::max(best,(long long)p.size());}while(std::next_permutation(p.begin(),p.end()));}
 }else if(problem=="p07"){
    for(int mask=0;mask<(1<<n);++mask){std::vector<std::pair<long long,long long>>p;for(int i=0;i<n;++i)if(mask>>i&1)p.push_back({a[i],b[i]});std::sort(p.begin(),p.end());
        long long reach=F;bool ok=true;for(auto x:p){if(x.first>reach){ok=false;break;}reach+=x.second;}if(ok&&reach>=B)best=std::min(best,(long long)p.size());}
    if(best==LLONG_MAX)best=-1;
 }else if(problem=="p08"){
    std::function<int(int)>f=[&](int mask){if(!mask)return 0;int i=0;while(!(mask>>i&1))++i;int rest=mask^(1<<i),ans=1+f(rest);for(int j=i+1;j<n;++j)if((rest>>j&1)&&a[i]+a[j]<=B)ans=std::min(ans,1+f(rest^(1<<j)));return ans;};
    best=f((1<<n)-1);
 }else throw std::runtime_error("unknown oracle");
 return best;
}
