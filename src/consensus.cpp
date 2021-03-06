/**
 * Copyright 2018 VMware
 * Copyright 2018 Ted Yin
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cassert>
#include <stack>

#include "hotstuff/util.h"
#include "hotstuff/consensus.h"

#define LOG_INFO HOTSTUFF_LOG_INFO
#define LOG_DEBUG HOTSTUFF_LOG_DEBUG
#define LOG_WARN HOTSTUFF_LOG_WARN
#define LOG_PROTO HOTSTUFF_LOG_PROTO

namespace hotstuff {

/* The core logic of HotStuff, is fairly simple :). */
/*** begin HotStuff protocol logic ***/
HotStuffCore::HotStuffCore(ReplicaID id,
                            privkey_bt &&priv_key):
        b0(new Block(true, 1)),
        b_lock(b0),
        b_exec(b0),
        vheight(0),
        priv_key(std::move(priv_key)),
        tails{b0},
        vote_disabled(false),
        id(id),
        storage(new EntityStorage()) {
    storage->add_blk(b0);
}

void HotStuffCore::sanity_check_delivered(const block_t &blk) {
    if (!blk->delivered)
        throw std::runtime_error("block not delivered");
}

block_t HotStuffCore::get_delivered_blk(const uint256_t &blk_hash) {
    block_t blk = storage->find_blk(blk_hash);
    if (blk == nullptr || !blk->delivered)
        throw std::runtime_error("block not delivered");
    return std::move(blk);
}

bool HotStuffCore::on_deliver_blk(const block_t &blk) {
    if (blk->delivered)
    {
        LOG_WARN("attempt to deliver a block twice");
        return false;
    }
    blk->parents.clear();
    for (const auto &hash: blk->parent_hashes)
        blk->parents.push_back(get_delivered_blk(hash));
    blk->height = blk->parents[0]->height + 1;

    if (blk->qc)
    {
        block_t _blk = storage->find_blk(blk->get_cmds().size()?blk->get_hash():blk->qc->get_obj_hash());
        if (_blk == nullptr)
            throw std::runtime_error("block referred by qc not fetched");
        blk->qc_ref = std::move(_blk);
    } // otherwise blk->qc_ref remains null

    for (auto pblk: blk->parents) tails.erase(pblk);
    tails.insert(blk);

    blk->delivered = true;
    LOG_DEBUG("deliver %s", std::string(*blk).c_str());
    return true;
}

void HotStuffCore::update_hqc(const block_t &_hqc, const quorum_cert_bt &qc) {
    if (_hqc->height > hqc.first->height)
    {
        hqc = std::make_pair(_hqc, qc->clone());
        on_hqc_update();
    }
}

void HotStuffCore::update(const block_t &nblk) {
    /* nblk = b*, blk2 = b'', blk1 = b', blk = b */
#ifndef HOTSTUFF_TWO_STEP
    /* three-step HotStuff */
    const block_t &blk2 = nblk->qc_ref;
    if (blk2 == nullptr) return;
    /* decided blk could possible be incomplete due to pruning */
    if (blk2->decision) return;
    update_hqc(blk2, nblk->qc);

    const block_t &blk1 = blk2->qc_ref;
    if (blk1 == nullptr) return;
    if (blk1->decision) return;
    if (blk1->height > b_lock->height) b_lock = blk1;

    const block_t &blk = blk1->qc_ref;
    if (blk == nullptr) return;
    if (blk->decision) return;

    /* commit requires direct parent */
    if (blk2->parents[0] != blk1 || blk1->parents[0] != blk) return;
#else
    /* two-step HotStuff */
    const block_t &blk1 = nblk->qc_ref;
    if (blk1 == nullptr) return;
    if (blk1->decision) return;
    update_hqc(blk1, nblk->qc);
    if (blk1->height > b_lock->height) b_lock = blk1;

    const block_t &blk = blk1->qc_ref;
    if (blk == nullptr) return;
    if (blk->decision) return;

    /* commit requires direct parent */
    if (blk1->parents[0] != blk) return;
#endif
    /* otherwise commit */
    std::vector<block_t> commit_queue;
    block_t b;
    for (b = blk; b->height > b_exec->height; b = b->parents[0])
    { /* TODO: also commit the uncles/aunts */
        commit_queue.push_back(b);
    }
    if (b != b_exec)
        throw std::runtime_error("safety breached :( " +
                                std::string(*blk) + " " +
                                std::string(*b_exec));
    for (auto it = commit_queue.rbegin(); it != commit_queue.rend(); it++)
    {
        const block_t &blk = *it;
        blk->decision = 1;
        do_consensus(blk);
        LOG_PROTO("commit %s", std::string(*blk).c_str());
        for (size_t i = 0; i < blk->cmds.size(); i++)
            do_decide(Finality(id, 1, i, blk->height,
                                blk->cmds[i], blk->get_hash()));
    }
    b_exec = blk;
}

block_t HotStuffCore::on_propose(const std::vector<uint256_t> &cmds,
                            const std::vector<block_t> &parents,
                            bytearray_t &&extra) {
    if (parents.empty())
        throw std::runtime_error("empty parents");
    for (const auto &_: parents) tails.erase(_);
    /* create the new block */
   
    block_t bnew = storage->add_blk(
        new Block(parents, cmds,
            hqc.second->clone(), std::move(extra),
            parents[0]->height + 1,
            hqc.first,
            nullptr
        ));
    
    //const uint256_t bnew_hash = bnew->get_hash();
    const uint256_t bnew_hash = bnew->get_cmds().size()?bnew->get_cmds()[0]:bnew->get_hash();
    bnew->self_qc = create_quorum_cert(bnew_hash);
    on_deliver_blk(bnew);
    update(bnew);
    Proposal prop(id, bnew, nullptr);
    LOG_PROTO("propose %s", std::string(*bnew).c_str());
    /* self-vote */
    /*if (bnew->height <= vheight)
        throw std::runtime_error("new block should be higher than vheight");*/
    vheight = bnew->height;
    on_receive_vote(
        Vote(id, bnew->get_hash(),
            create_part_cert(*priv_key, bnew_hash), this));
    on_propose_(prop);
    /* boradcast to other replicas */
    LOG_INFO("send %s",std::string(prop).c_str());
    do_broadcast_proposal(prop);
    return bnew;
}
bool HotStuffCore::check_cmds(std::vector<uint256_t> cmds){
    uint8_t milestone_sendbuf[162];
    for(int i = 0; i < cmds.size(); i++){

        bytearray_t arr_cmd = cmds[i].to_bytes();
        LOG_INFO("cmds %d : %s , leng : %d", i, get_hex10(cmds[i]).c_str(), arr_cmd.size());
        if(i == 5){
            milestone_sendbuf[i*32 + 0] = arr_cmd[0];
            milestone_sendbuf[i*32 + 1] = arr_cmd[1];
            continue;
        }
        for(int j = 0; j < arr_cmd.size(); j++){
            milestone_sendbuf[i*32 + j] = arr_cmd[j];
        }
            
    }
    bool is_cmds_legal = false;
    Coo::send_data(send_port_for_iri, milestone_sendbuf, 162); 
    if(Coo::listening_iri(is_cmds_legal)){
        if(is_cmds_legal){
            return true;
        }
    }
    return false;
}
void HotStuffCore::on_receive_proposal(const Proposal &prop) {
    LOG_PROTO("got %s", std::string(prop).c_str());
    block_t bnew = prop.blk;
    /*
    *check blk hash
    */
    
    /*bool find_target = false;
    for( const auto& ele : decision_waiting_with_none_client ) {
        LOG_INFO("ele hash is:%s\n",get_hex10(ele.first).c_str());
        if(strcmp(get_hex10(ele.first).c_str(),get_hex10(bnew->get_cmds()[0]).c_str())==0){
            find_target = true;
        }
    }*/
    /*if(!find_target){
        LOG_INFO("cannot find_target");
        return;  
    }*/ 
    sanity_check_delivered(bnew);
    update(bnew);
    bool opinion = false;
    if (bnew->height > vheight)
    {
        if (bnew->qc_ref && bnew->qc_ref->height > b_lock->height)
        {
            opinion = true; // liveness condition
            vheight = bnew->height;
        }
        else
        {   // safety condition (extend the locked branch)
            block_t b;
            for (b = bnew;
                b->height > b_lock->height;
                b = b->parents[0]);
            if (b == b_lock) /* on the same branch */
            {
                opinion = true;
                vheight = bnew->height;
            }
        }
    }
    LOG_PROTO("now state: %s", std::string(*this).c_str());
    if (bnew->qc_ref)
        on_qc_finish(bnew->qc_ref);
    on_receive_proposal_(prop);
    if(decision_waiting_with_none_client.size()&&bnew->get_cmds().size()){
        uint8_t vote_sendbuf[1];
        vote_sendbuf[0] = 18;
        LOG_INFO("now return msg size: %d\n", 1);
        decision_waiting_with_none_client.clear();
        Coo::send_data(send_port_for_coo, vote_sendbuf, 1);    
    }
    if (opinion && !vote_disabled){
        if(bnew->get_cmds().size()){
            if(check_cmds(bnew->get_cmds())){
                do_vote(prop.proposer,
                    Vote(id, bnew->get_hash(),
                        create_part_cert(*priv_key, bnew->get_cmds()[0]), this));
            }
        }else{
            do_vote(prop.proposer,
                Vote(id, bnew->get_hash(),
                    create_part_cert(*priv_key, bnew->get_hash()), this));
        }
        
    }
}

void HotStuffCore::on_receive_vote(const Vote &vote) {
    LOG_PROTO("got %s", std::string(vote).c_str());
    LOG_PROTO("now state: %s", std::string(*this).c_str());
    block_t blk = get_delivered_blk(vote.blk_hash);
    assert(vote.cert);
    size_t qsize = blk->voted.size();
    if (qsize >= config.nmajority) return;
    if (!blk->voted.insert(vote.voter).second){
        LOG_WARN("duplicate vote for %s from %d", get_hex10(vote.blk_hash).c_str(), vote.voter);
        return;
    }
    auto &qc = blk->self_qc;
    if (qc == nullptr){
        LOG_WARN("vote for block not proposed by itself");
        if(blk->get_cmds().size()){
            qc = create_quorum_cert(blk->get_cmds()[0]);    
        }else{
            qc = create_quorum_cert(blk->get_hash());    
        }
        
    }
    qc->add_part(vote.voter, *vote.cert);
    if (qsize + 1 == config.nmajority){
        /*
        * send back to Coordinator
        * i:int idx
        * qc->sigs[i]->data: uint8_t[64] signeture
        * query decision_waiting_with_none_client
        */
        if(decision_waiting_with_none_client.size()&&blk->get_cmds().size()){
            if(decision_waiting_with_none_client.find(blk->get_cmds()[0]) != decision_waiting_with_none_client.end()){
                uint8_t sendbuf[1024*1024];
                /*uint32_t idx = decision_waiting_with_none_client[qc->get_obj_hash()];
                sendbuf[0] = (uint8_t)(idx / 256);
                sendbuf[1] = (uint8_t)(idx % 256);*/

                for(int i = 0; i < 32; i++){
                    sendbuf[i] = std::vector<uint8_t>(qc->get_obj_hash())[i];
                }
                int itr = 32;
                for (size_t i = 0; i < qc->get_rids().size(); i++){
                    if (qc->get_rids().get(i)){
                        uint8_t buff[100];
                        size_t len;
                        sendbuf[itr] = (uint8_t)i;
                        itr ++;
                        LOG_INFO("now return id: %u\n", (uint8_t)i);
                        qc->sigs[i].serialize(buff,len);
                        sendbuf[itr] = (uint8_t)len;
                        itr ++;
                        memcpy(sendbuf + itr, buff, (uint8_t)len);
                        itr += (uint8_t)len;
                    }
                }
                LOG_INFO("now return msg size: %d\n",  itr);
                Coo::send_data(send_port_for_coo, sendbuf, itr);
            }
            
        }
        
        qc->compute();
        update_hqc(blk, qc);
        on_qc_finish(blk);
    }
}
/*** end HotStuff protocol logic ***/
void HotStuffCore::on_init(uint32_t nfaulty) {
    config.nmajority = config.nreplicas - nfaulty;
    if(b0->get_cmds().size()){
        b0->qc = create_quorum_cert(b0->get_cmds()[0]/*b0->get_hash()*/);    
    }else{
        b0->qc = create_quorum_cert(b0->get_hash());
    }
    
    b0->qc->compute();
    b0->self_qc = b0->qc->clone();
    b0->qc_ref = b0;
    hqc = std::make_pair(b0, b0->qc->clone());
}

void HotStuffCore::prune(uint32_t staleness) {
    block_t start;
    /* skip the blocks */
    for (start = b_exec; staleness; staleness--, start = start->parents[0])
        if (!start->parents.size()) return;
    std::stack<block_t> s;
    start->qc_ref = nullptr;
    s.push(start);
    while (!s.empty())
    {
        auto &blk = s.top();
        if (blk->parents.empty())
        {
            storage->try_release_blk(blk);
            s.pop();
            continue;
        }
        blk->qc_ref = nullptr;
        s.push(blk->parents.back());
        blk->parents.pop_back();
    }
}

void HotStuffCore::add_replica(ReplicaID rid, const NetAddr &addr,
                                pubkey_bt &&pub_key) {
    config.add_replica(rid, 
            ReplicaInfo(rid, addr, std::move(pub_key)));
    b0->voted.insert(rid);
}

promise_t HotStuffCore::async_qc_finish(const block_t &blk) {
    if (blk->voted.size() >= config.nmajority)
        return promise_t([](promise_t &pm) {
            pm.resolve();
        });
    auto it = qc_waiting.find(blk);
    if (it == qc_waiting.end())
        it = qc_waiting.insert(std::make_pair(blk, promise_t())).first;
    return it->second;
}

void HotStuffCore::on_qc_finish(const block_t &blk) {
    auto it = qc_waiting.find(blk);
    if (it != qc_waiting.end())
    {
        it->second.resolve();
        qc_waiting.erase(it);
    }
}

promise_t HotStuffCore::async_wait_proposal() {
    return propose_waiting.then([](const Proposal &prop) {
        return prop;
    });
}

promise_t HotStuffCore::async_wait_receive_proposal() {
    return receive_proposal_waiting.then([](const Proposal &prop) {
        return prop;
    });
}

promise_t HotStuffCore::async_hqc_update() {
    return hqc_update_waiting.then([this]() {
        return hqc.first;
    });
}

void HotStuffCore::on_propose_(const Proposal &prop) {
    auto t = std::move(propose_waiting);
    propose_waiting = promise_t();
    t.resolve(prop);
}

void HotStuffCore::on_receive_proposal_(const Proposal &prop) {
    auto t = std::move(receive_proposal_waiting);
    receive_proposal_waiting = promise_t();
    t.resolve(prop);
}

void HotStuffCore::on_hqc_update() {
    auto t = std::move(hqc_update_waiting);
    hqc_update_waiting = promise_t();
    t.resolve();
}

HotStuffCore::operator std::string () const {
    DataStream s;
    s << "<hotstuff "
      << "hqc=" << get_hex10(hqc.first->get_hash()) << " "
      << "hqc.height=" << std::to_string(hqc.first->height) << " "
      << "b_lock=" << get_hex10(b_lock->get_hash()) << " "
      << "b_exec=" << get_hex10(b_exec->get_hash()) << " "
      << "vheight=" << std::to_string(vheight) << " "
      << "tails=" << std::to_string(tails.size()) << ">";
    return std::move(s);
}

}
