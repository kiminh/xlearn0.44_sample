//
// Created by ming.li on 19-12-4.
//

#include "src/modelParameter/parameters.h"
namespace youtubDnn {
    void Model::Initialize(index_t num_feature,
                                      index_t num_field,
                                      index_t num_K,
                                      index_t *fullLayer_Cells,
                                      index_t threadNumber,
                                      real_t scale = 1.0) {
        num_feat_ = num_feature;
        num_field_ = num_field;
        num_K_ = num_K;
        aligned_k_ = (index_t) ceil((real_t) num_K_ / kAlign) * kAlign;
        threadNumber_ = threadNumber;
        scale_ = scale;

        /****************************
         * embedding vector: feature * K
         *****************************/
        embedding_num_v_ = num_feature * aligned_k_;


        /****************************
         * fulllayer weight
         *****************************/
        fullLayer_Cells_ = fullLayer_Cells;
        num_fullLayer_Cells_ = sizeof(fullLayer_Cells_) / sizeof(index_t);
        fulllayer_w_ = std::vector<real_t *>(num_fullLayer_Cells_, nullptr);
        fulllayer_w_change_ = std::vector<std::vector<real_t *>>(threadNumber_,std::vector<real_t *>(num_fullLayer_Cells_, nullptr));
        fulllayer_num_w_ = std::vector<index_t>(num_fullLayer_Cells_, 0);
        fulllayer_best_w_ = std::vector<real_t *>(num_fullLayer_Cells_, nullptr);
        fulllayer_b_ = std::vector<real_t *>(num_fullLayer_Cells_, nullptr);
        fulllayer_b_change_ = std::vector<std::vector<real_t *>>(threadNumber_,std::vector<real_t *>(num_fullLayer_Cells_, nullptr));
        fulllayer_num_b_ = std::vector<index_t>(num_fullLayer_Cells_, 0);
        fulllayer_best_b_ = std::vector<real_t *>(num_fullLayer_Cells_, nullptr);
        fulllayer_change_num_ = nullptr;
        index_t pre_Num_layer_j = (num_field_ - 1) * aligned_k_;
        for (index_t layer_j = 0; layer_j < num_fullLayer_Cells_; ++layer_j) {
            index_t Num_layer_j = *(fullLayer_Cells_ + layer_j);
            fulllayer_num_w_[layer_j] = pre_Num_layer_j * Num_layer_j;
            fulllayer_num_b_[layer_j] = Num_layer_j;
            pre_Num_layer_j = Num_layer_j;
        }


        /****************************
         * 系数变化
         *****************************/
        int ret7 = posix_memalign(
                (void **) &fulllayer_change_num_,
                ((kAlignByte) > (16) ? (kAlignByte) : (16)),// max(kAlignByte,16)
                threadNumber_ * sizeof(real_t));
        if (ret7 != 0) exit(0);
        for (index_t threadi = 0; threadi < threadNumber_; ++threadi) {
            for (index_t layer_j = 0; layer_j < num_fullLayer_Cells_; ++layer_j) {
                int ret5 = posix_memalign(
                        (void **) &fulllayer_w_change_[threadi][layer_j],
                        ((kAlignByte) > (16) ? (kAlignByte) : (16)),// max(kAlignByte,16)
                        fulllayer_num_w_[layer_j] * sizeof(real_t));
                if (ret5 != 0) exit(0);
                for (index_t i = 0; i < fulllayer_num_w_[layer_j]; i++) {
                    *(fulllayer_w_change_[threadi][layer_j] + i) = 0.0;
                }
                int ret6 = posix_memalign(
                        (void **) &fulllayer_b_change_[threadi][layer_j],
                        ((kAlignByte) > (16) ? (kAlignByte) : (16)),// max(kAlignByte,16)
                        fulllayer_num_b_[layer_j] * sizeof(real_t));
                if (ret6 != 0) exit(0);
                for (index_t i = 0; i < fulllayer_num_b_[layer_j]; i++) {
                    *(fulllayer_b_change_[threadi][layer_j] + i) = 0.0;
                }
            }
            *(fulllayer_change_num_ + threadi) = 0.0;
        }

        /****************************
         * midScore_threads_
         *****************************/
        midScore_threads_ = std::vector<std::vector<real_t*>>(threadNumber_, std::vector<real_t*>(2+num_fullLayer_Cells_,nullptr));
        midScore_num_threads_ = std::vector<index_t>(2+num_fullLayer_Cells_,0);
        index_t total_midScore=0;
        for (index_t threadi=0;threadi < threadNumber_; ++threadi){
            midScore_num_threads_[0]=aligned_k_;                                      //  TargitRidEmbedding
            midScore_num_threads_[1]=aligned_k_*(num_field_-1);                       //  OthersEmbedding
            total_midScore += midScore_num_threads_[0];
            total_midScore += midScore_num_threads_[1];
            for (index_t layer_j = 0; layer_j < num_fullLayer_Cells_; ++layer_j) {
                midScore_num_threads_[2+layer_j]= *(fullLayer_Cells_+layer_j);       //  fulllayer
                total_midScore += midScore_num_threads_[2+layer_j];
            }
        }
        index_t every_midScore= total_midScore/threadNumber_ ;
        real_t *midScore=new real_t[total_midScore]{0.0};
        for (index_t threadi=0;threadi < threadNumber_; ++threadi){
            midScore_threads_[threadi][0]    = midScore+threadi*every_midScore;                                        //  TargitRidEmbedding
            midScore_threads_[threadi][1]    = midScore_threads_[threadi][0] + midScore_num_threads_[0];      //  OthersEmbedding
            for (index_t layer_j = 0; layer_j < num_fullLayer_Cells_; ++layer_j) {  //  fulllayer
                midScore_threads_[threadi][2+layer_j]    = midScore_threads_[threadi][1+layer_j] + midScore_num_threads_[1+layer_j];
            }
        }


        /****************************
         * initial
        *****************************/
        this->initial(true);

    }

    // Initialize model from a checkpoint file
    Model::Model(const std::string& filename,youtubDnn::HyperParam& hyper_param_) {
        if (this->DeserializeFromTxt(filename,hyper_param_) == false) {
            std::cout<< "Cannot Load model from the file:" << filename.c_str() <<"\n";
            exit(0);
        }
    }


    // To get the best performance for SSE, we need to
    // allocate memory for the model parameters in aligned way.
    // For SSE, the align number should be 16 byte (kAlignByte).
    void Model::initial(bool set_val) {
        try {
            /*********************************
             * embedding
             **********************************/
            int ret1 = posix_memalign(
                    (void**)&embedding_v_,
                    ((kAlignByte)>(16)?(kAlignByte):(16)),// max(kAlignByte,16)
                    embedding_num_v_ * sizeof(real_t));
            if (ret1 != 0) exit(0);
            int ret2 = posix_memalign(
                    (void**)&embedding_best_v_,
                    ((kAlignByte)>(16)?(kAlignByte):(16)),// max(kAlignByte,16),
                    embedding_num_v_ * sizeof(real_t));
            if (ret2 != 0) exit(0);

            /*********************************
             * fulllayer
             **********************************/
            for (index_t layer_j = 0; layer_j < num_fullLayer_Cells_; ++layer_j) {
                // 系数
                int ret1 = posix_memalign(
                        (void **) &fulllayer_w_[layer_j],
                        ((kAlignByte) > (16) ? (kAlignByte) : (16)),// max(kAlignByte,16)
                        fulllayer_num_w_[layer_j] * sizeof(real_t));
                if (ret1 != 0) exit(0);
                int ret2 = posix_memalign(
                        (void **) &fulllayer_b_[layer_j],
                        ((kAlignByte) > (16) ? (kAlignByte) : (16)),// max(kAlignByte,16)
                        fulllayer_num_b_[layer_j] * sizeof(real_t));
                if (ret2 != 0) exit(0);
                // best系数
                int ret3 = posix_memalign(
                        (void **) &fulllayer_best_w_[layer_j],
                        ((kAlignByte) > (16) ? (kAlignByte) : (16)),// max(kAlignByte,16),
                        fulllayer_num_w_[layer_j] * sizeof(real_t));
                if (ret3 != 0) exit(0);
                int ret4 = posix_memalign(
                        (void **) &fulllayer_best_b_[layer_j],
                        ((kAlignByte) > (16) ? (kAlignByte) : (16)),// max(kAlignByte,16),
                        fulllayer_num_b_[layer_j] * sizeof(real_t));
                if (ret4 != 0) exit(0);
            }
        } catch (std::bad_alloc&) {
            std::cout << "Cannot allocate enough memory for current model parameters. Parameter size: " <<"\n";
        }
        // Set value for model
        if (set_val) {
            set_value();
        }
    }


    // Set value for model
    void Model::set_value() {
        // Use distribution to transform the random unsigned
        // int generated by gen into a float in [(0.0, 1.0) * coef]
        std::default_random_engine generator;
        //std::uniform_real_distribution<real_t> dis(0.0, 1.0);
        std::uniform_real_distribution<real_t> dis(-1.0, 1.0);

        /*********************************************************
         *  Initialize embedding                                 *
         *********************************************************/
        real_t coef = 1.0f / sqrt(num_K_) * scale_;
        real_t* w = embedding_v_;
        for (index_t j = 0; j < num_feat_; ++j) {
            for(index_t d = 0; d < aligned_k_; d++, w++) {
                *w = (d < num_K_) ? (coef * dis(generator)):0.0;  // Beyond aligned number set 0.0
            }
        }

        /*********************************************************
         *  fuller layer  weights
         *
         *  layer0:                  cell0       cell1   cell2                      (Num_layer_j=3)
         *                              \          /     /
         *                        w[k+0] \  w[k+1]/     /w[k+2]---- 同输入celli相连的w 是 一段连续地址
         *                                \      /     /
         *                                 \    |    |
         *                   -------------- |   |    | ------------------------    ---------------------------------------------------     --------------------------------------------------
         *  filed:embedding: | filed1:em0  filed1:em1  filed1:em2  filed1:em3 |   | filed2:em0  filed2:em1  filed2:em2   filed2:em3  |     | filed3:em0  filed3:em1  filed3:em2  filed3:em3 |
         *         |         --------------------------------------------------    --------------------------------------------------      --------------------------------------------------
         *         |
         *  (pre_Num_layer_j=(num_field_ -1)* num_K_=4)  , 注意:field0是targitRid不经过全连层
         *
         *********************************************************/
        index_t pre_Num_layer_j= (num_field_-1) * aligned_k_;
        for (index_t layer_j = 0; layer_j < GetNumFullLayerCell(); ++layer_j) {
            real_t* w= fulllayer_w_[layer_j];
            real_t* b= fulllayer_b_[layer_j];
            // cells in this layer
            index_t Num_layer_j= *(fullLayer_Cells_+layer_j);
            real_t coef = 1.0f / sqrt(Num_layer_j) * scale_;
            // weight and bias
            for (index_t input_i =0; input_i <= pre_Num_layer_j; ++input_i){
                if(input_i == 0){
                    for(index_t d = 0; d < get_aligned(Num_layer_j); d++, b++) {
                        *b = (d < Num_layer_j) ? (coef * dis(generator)) : 0.0;  // Beyond aligned number set 0.0
                    }
                }else{
                    for(index_t d = 0; d < get_aligned(Num_layer_j); d++, w++) {
                        *w = (d < Num_layer_j) ? (coef * dis(generator)) : 0.0; // Beyond aligned number set 0.0
                    }
                }
            }
            pre_Num_layer_j = Num_layer_j;
        }
    }


    // Free the allocated memory
    void Model::free_model() {

        free(embedding_v_);
        if (embedding_best_v_ != nullptr) {
            free(embedding_best_v_);
        }

        for (index_t layer_j = 0; layer_j < GetNumFullLayerCell(); ++layer_j){
            //
            free(fulllayer_w_[layer_j]);
            free(fulllayer_b_[layer_j]);
            //
            if(fulllayer_best_w_[layer_j] != nullptr){
                free(fulllayer_best_w_[layer_j]);
            }
            if(fulllayer_best_b_[layer_j] != nullptr){
                free(fulllayer_best_b_[layer_j]);
            }
            //
            for (index_t threadi=0;threadi < threadNumber_; ++threadi){
                if(fulllayer_w_change_[threadi][layer_j] != nullptr){
                    free(fulllayer_w_change_[threadi][layer_j]);
                }
                if(fulllayer_b_change_[threadi][layer_j] != nullptr){
                    free(fulllayer_b_change_[threadi][layer_j]);
                }
            }

        }
    }


    // Initialize model from a checkpoint file
//    youtubeDnn_Model::Model(const std::string& filename,youtubDnn::youtubDnn_HyperParam& hyper_param_,bool para_isjustloadembedding) {
//        if (DeserializeFromTxt(filename,hyper_param_,para_isjustloadembedding) == false) {
//            std::cout<< "Cannot Load model from the file:"<< filename.c_str()<<"\n";
//            exit(0);
//        }
//    }


    // Serialize current model to a TXT file.
    void Model::SerializeToTXT(const std::string& filename) {
        std::ofstream o_file(filename);

        /*********************************************************
         *  Write harg                    *
         *********************************************************/
        o_file << "num_feat_:" << num_feat_ << "\n";
        o_file << "num_field_:" << num_field_ << "\n";
        o_file << "num_K_:" << num_K_ << "\n";

        /*********************************************************
         *  Write embedding                                      *
         *********************************************************/
        real_t* em_w = embedding_v_;
        for (index_t j = 0; j < num_feat_; ++j) {
            o_file << "embedding_" << j << ":";
            for(index_t d = 0; d < num_K_; d++, em_w++) {
                o_file << *em_w;
                if (d != (num_K_-1)) {
                    o_file << ",";
                }
            }
            o_file << "\n";
            // skip the rest parameters
            index_t skip = aligned_k_-num_K_;
            em_w += skip;
        }


        /*********************************************************
         *  Write fulllayer weights  and bias                    *
         *  先写第1层的bias然后是weights,weights的顺序是按照输入来循环的
         *  然后是第2层,层内顺序一致
         *********************************************************/
        index_t pre_Num_layer_j= (num_field_-1) * aligned_k_;
        for (index_t layer_j = 0; layer_j < num_fullLayer_Cells_; ++layer_j) {
            index_t Num_layer_j = *(fullLayer_Cells_+layer_j);
            // bias
            real_t* b= fulllayer_b_[layer_j];
            if(true){
                o_file << "bias_" << layer_j << ":";
                for(index_t j=0; j < Num_layer_j ; j++){ // output
                    o_file << *b;
                    if(j != (Num_layer_j-1)){
                        o_file << ",";
                    }
                    b += 1;
                }
                o_file << "\n";
            }
            // weights
            real_t* w= fulllayer_w_[layer_j];
            for(index_t i=0; i < pre_Num_layer_j ; i++){ // input
                o_file << "weight_"<<layer_j << "_" << i << ":";
                for(index_t j=0; j < Num_layer_j ; j++){ // output
                    o_file << *w;
                    if(j != (Num_layer_j-1)){
                        o_file << ",";
                    }
                    w += 1;
                }
                o_file << "\n";
            }
            pre_Num_layer_j = Num_layer_j;
        }

    }

/*
 * new add
 * */
    std::string Model::getStringFromString(std::ifstream &i_file, const std::string key, const size_t LINE_LENGTH){
        char charn[LINE_LENGTH];
        i_file.getline(charn,LINE_LENGTH);
        std::string line(charn);
        if(line.find(key)==0){
            std::string value_=line.substr(key.size(),-1);
            return value_;
        }else{
            return "";
        };
    }
    bool Model::setStringFromString(std::ifstream &i_file,
                                    const std::string key,
                                    const size_t LINE_LENGTH,
                                    std::string &var){
        std::string value_=getStringFromString(i_file,key,LINE_LENGTH);
        if(value_.compare("")==0){
            return false;
        }else{
            var=value_;
            return true;
        };
    }
    bool Model::setIndextFromString(std::ifstream &i_file,
                                    const std::string key,
                                    const size_t LINE_LENGTH,
                                    index_t &var){
        std::string value_=getStringFromString(i_file,key,LINE_LENGTH);
        if(value_.compare("")==0){
            return false;
        }else{
            var=atoi(value_.c_str());
            return true;
        };
    }

    index_t Model::setFloattVectorFromString(std::ifstream &i_file,const std::string key, const size_t LINE_LENGTH,real_t* w_ptr){
        std::string value =getStringFromString(i_file,key,LINE_LENGTH);
        // split
        //char* x=new char[value.length()];-----> 有内存问题
        char x[value.length()];
        strcpy(x,value.c_str());
        char* pch=strtok(x,",");
        index_t cnt_tmp=0;
        while(pch!=NULL){
            std::string vaule_split(pch);
            *w_ptr = atof(vaule_split.c_str());
            w_ptr += 1;  // 函数外部不起作用
            cnt_tmp += 1;
            pch=strtok(NULL,",");
        }
        return cnt_tmp;
    }

    bool Model::DeserializeEmbeddingFromTxt(const std::string &filename) {
        std::ifstream i_file(filename);
        const  size_t  LINE_LENGTH =  10240 ;
        /*********************************************************
         *  loadding embedding                                      *
         *********************************************************/
        real_t* em_w = embedding_v_;
        for (index_t j = 0; j < num_feat_; ++j) {
            std::string idx_str=std::to_string(j);
            std::string key="embedding_"+idx_str+":";
            index_t setnums=setFloattVectorFromString(i_file,key,LINE_LENGTH,em_w);
            if(setnums==num_K_){
                em_w +=  setnums ;
                // skip the rest parameters
                index_t skip = aligned_k_-num_K_;
                em_w += skip;
            }else{
                return false;
            }
        }
        return true;
    }


    bool Model::DeserializeFromTxt(const std::string& filename,
                                   youtubDnn::HyperParam& hyper_param_) {
        std::ifstream i_file(filename);
        const  size_t  LINE_LENGTH =  10240 ;

        /*********************************************************
         *  read h_param                                         *   not include fullLayer_Cells  ???????
         *********************************************************/
        if(not setIndextFromString(i_file,"num_feat_:",LINE_LENGTH,hyper_param_.num_feature)){
            return false;
        }
        if(not setIndextFromString(i_file,"num_field_:",LINE_LENGTH,hyper_param_.num_field)){
            return false;
        }
        if(not setIndextFromString(i_file,"num_K_:",LINE_LENGTH,hyper_param_.num_K)){
            return false;
        }

        /*********************************************************
         *  Initialize model                                     *
         *********************************************************/
        this->Initialize(hyper_param_.num_feature,
                         hyper_param_.num_field,
                         hyper_param_.num_K,
                         hyper_param_.fullLayer_Cells,
                         hyper_param_.thread_number,
                         hyper_param_.model_scale);
        this->initial(false);

        /*********************************************************
         *  loadding embedding                                      *
         *********************************************************/
        real_t* em_w = embedding_v_;
        for (index_t j = 0; j < num_feat_; ++j) {
            std::string idx_str=std::to_string(j);
            std::string key="embedding_"+idx_str+":";
            index_t setnums=setFloattVectorFromString(i_file,key,LINE_LENGTH,em_w);
            if(setnums==num_K_){
                em_w +=  setnums ;
                // skip the rest parameters
                index_t skip = aligned_k_-num_K_;
                em_w += skip;
            }else{
                return false;
            }
        }

        /*********************************************************
        *  loadding fulllayer weights and bias                   *
        *********************************************************/
        index_t pre_Num_layer_j= (num_field_-1) * aligned_k_;
        for (index_t layer_j = 0; layer_j < num_fullLayer_Cells_; ++layer_j) {
            index_t Num_layer_j = *(fullLayer_Cells_+layer_j);
            std::string layer_j_str = std::to_string(layer_j);
            // bias
            real_t* b= fulllayer_b_[layer_j];
            if(true){
                std::string key="bias_"+layer_j_str+":";
                index_t setnums=setFloattVectorFromString(i_file,key,LINE_LENGTH,b);
                if(setnums != Num_layer_j){
                    return false;
                }
            }
            // weights
            real_t* w= fulllayer_w_[layer_j];
            for(index_t i=0; i<pre_Num_layer_j ; i++){
                std::string i_str = std::to_string(i);
                std::string key="weight_"+layer_j_str+"_"+i_str+":";
                index_t setnums=setFloattVectorFromString(i_file,key,LINE_LENGTH,w);
                if(setnums == Num_layer_j){
                    w +=  setnums ;
                }else{
                    return false;
                }
            }
            pre_Num_layer_j = Num_layer_j ;
        }
        return true;
    }


    // Take a record of the best model during training
    void Model::SetBestModel() {
        index_t num_fullLayer =GetNumFullLayerCell();
        // Copy current model parameters
        memcpy(embedding_best_v_, embedding_v_, embedding_num_v_*sizeof(real_t));
        for (index_t layer_j = 0; layer_j < num_fullLayer; ++layer_j) {
            memcpy(fulllayer_best_w_[layer_j], fulllayer_w_[layer_j], fulllayer_num_w_[layer_j] * sizeof(real_t));
            memcpy(fulllayer_best_b_[layer_j], fulllayer_b_[layer_j], fulllayer_num_b_[layer_j] * sizeof(real_t));
        }
    }

    // Shrink back for getting the best model
    void Model::Shrink() {
        // Copy best model parameters
        if (embedding_best_v_ != nullptr) {
            memcpy(embedding_v_, embedding_best_v_, embedding_num_v_*sizeof(real_t));
            for (index_t layer_j = 0; layer_j < GetNumFullLayerCell(); ++layer_j) {
                memcpy(fulllayer_w_[layer_j], fulllayer_best_w_[layer_j], fulllayer_num_w_[layer_j] * sizeof(real_t));
                memcpy(fulllayer_b_[layer_j], fulllayer_best_b_[layer_j], fulllayer_num_b_[layer_j] * sizeof(real_t));
            }
        }
    }


}
