#include "Joiner.h"

using namespace std;

Joiner::Joiner(Query* query, Predicate* predicate, int* is_processed, vector<uint64_t>* result_buffer):
query(query), predicate(predicate),
is_processed(is_processed), result_buffer(result_buffer){
    //sysconf(_SC_LEVEL1_DCACHE_LINESIZE) get l1 cache size
    h1_num_of_buckets = 512;
    h1_num_of_bits = (int)log2(h1_num_of_buckets);
    h2_num_of_buckets = 16699;
    h2_num_of_bits = (int)log2(h2_num_of_buckets);
}

int Joiner::segmentation(){
    //anagnorizoume poiia peritosi einai
    if(is_original[predicate->relation1])
    for(int i = 0; i < 2; i++){
        create_and_compute_hist_array(relations[i]);
        create_and_compute_psum_array(relations[i]);
        create_and_compute_new_column(relations[i]);
    }

    cout << "Both relations segmentated successfully!" << endl;
    return 0;
}

int Joiner::create_and_compute_hist_array(Relation* relation){
    //to make code more readable
    int hash_value;

    //allocate relation's hist_array and initialise
    relation->set_hist_array(h1_num_of_buckets);
    for(int j = 0; j < h1_num_of_buckets; j++)//TODO: memset
        relation->get_hist_array()[j] = 0;

    //compute hist_array
    for(int j = 0, limit = relation->get_num_of_records(); j < limit; j++){
        hash_value = h1(relation->get_column()[j]);
        relation->get_hist_array()[hash_value]++;
    }
    return 0;

}

int Joiner::create_and_compute_psum_array(Relation* relation){
    //allocate psum_array
    relation->set_psum_array(h1_num_of_buckets);

    //calculate psum_array
    relation->get_psum_array()[0] = 0;
    for(int j = 1; j < h1_num_of_buckets; j++)
        relation->get_psum_array()[j] = relation->get_psum_array()[j-1] + relation->get_hist_array()[j-1];
    return 0;
}

int Joiner::create_and_compute_new_column(Relation* relation){
    //to make code more readable
    int hash_value;

    //dynamically allocate new_column to store the column by hash value order
    relation->set_new_column(relation->get_num_of_records());

    //temporal array needed to calculate new_column. this array is copy of psum array initially
    int copy_of_psum_array[h1_num_of_buckets];
    for(int j = 0; j < h1_num_of_buckets; j++)
        copy_of_psum_array[j] = relation->get_psum_array()[j];

    //calculate new_column. new column is an array of tuples (uint64_t index, uint64_t value)
    for(int j = 0; j < relation->get_num_of_records(); j++){
        hash_value = h1(relation->get_column()[j]);
        relation->get_new_column()[copy_of_psum_array[hash_value]].set(j, relation->get_column()[j]);
        copy_of_psum_array[hash_value]++;
    }
    return 0;
}

int Joiner::indexing(){
    //choose the smallest relation to index
    Relation* relation = NULL;
    if(relations[0]->get_num_of_records() < relations[1]->get_num_of_records())
        relation = relations[0];
    else
        relation = relations[1];

    //create index for every bucket made at segmentation
    relation->set_index_array(h1_num_of_buckets);

    //to make code more readable
    int hash_value;

    //for every bucket made at segmentation
    for(int j = 0; j < h1_num_of_buckets; j++){
        //store current index to a variable to make code more readable
        Index& cur_index = relation->get_index_array()[j];
        create_and_init_chain_and_bucket_array(cur_index, relation->get_hist_array()[j]);
        //scan whole bucket in order to calculate its chain and bucket array
        int upper_limit = relation->get_psum_array()[j] + relation->get_hist_array()[j];
        for(int i = relation->get_psum_array()[j]; i < upper_limit; i++){
            hash_value = h2(relation->get_new_column()[i].get_value());
            cur_index.get_chain_array()[i - relation->get_psum_array()[j]] = cur_index.get_bucket_array()[hash_value];
            cur_index.get_bucket_array()[hash_value] = i - relation->get_psum_array()[j];
        }
    }
    cout << "Indexing completed successfully!" << endl;
    return 0;
}

int Joiner::create_and_init_chain_and_bucket_array(Index& index, int hist_array_value){
    //create bucket array and initialise it
    index.set_bucket_array(h2_num_of_buckets);
    for(int i = 0; i < h2_num_of_buckets; i++)
        index.get_bucket_array()[i] = -1; //-1 means that there is not previous record in the bucket

    //create chain array and initialise it
    index.set_chain_array(hist_array_value);
    for(int i = 0; i < hist_array_value; i++)
        index.get_chain_array()[i] = 0;

    return 0;
}

int Joiner::join(){
    //r0 --> NOT Indexed relation
    Relation* r0 = NULL;
    //r1 --> Indexed relation
    Relation* r1 = NULL;

    if(relations[0]->get_index_array() != NULL){
        r1 = relations[0];
        r0 = relations[1];
    }
    else{
        r1 = relations[1];
        r0 = relations[0];
    }

    //to store result
    this->result = new OutputList();

    int counter = 0;
    //for every row in r0
    for(int i = 0; i < r0->get_num_of_records(); i++){
        //for easier reading of code
        NewColumnEntry cur_row = r0->get_new_column()[i];

        //take the bucket needed
        int bucket_num = h1(cur_row.get_value());

        //search index for this record
        int index = r1->get_index_array()[bucket_num].get_bucket_array()[h2(cur_row.get_value())];

        //cout << "Value : " << cur_row.get_value() << endl;
        while(index != -1){
            //cout << r1.new_column[index].value << " vs "  << cur_row.value << endl;
            if(r1->get_new_column()[index + r1->get_psum_array()[bucket_num]].get_value() == cur_row.get_value()){
                //tuple [cur_row.get_index() + 1, r1->get_new_column()[index + r1->get_psum_array()[bucket_num]].get_index() + 1]
                //cout <<"[" << cur_row.get_index() + 1 << " : "<<r1->get_new_column()[index + r1->get_psum_array()[bucket_num]].get_index() + 1 <<"]"<< endl;
                uint64_t row1 = cur_row.get_index() + 1;
                uint64_t row2 = r1->get_new_column()[index + r1->get_psum_array()[bucket_num]].get_index() + 1;
                //printf("%lu == %lu\n",row1,row2);
                this->result->InsertData(row1, row2);
            }
            index = r1->get_index_array()[bucket_num].get_chain_array()[index];
        }
        //cout << " -----------------------------" << endl;
    }
    return 0;
}

Joiner::~Joiner(){
    Index* index;
    if(relations[0]->get_index_array() != NULL)
        index = relations[0]->get_index_array();
    else
        index = relations[1]->get_index_array();

    // if(index != NULL){
    //   for(int i = 0; i < h1_num_of_buckets; i++){
    //     if(index[i].get_chain_array() != NULL)
    //       free(index[i].get_chain_array());
    //     if(index[i].get_bucket_array() != NULL)
    //       free(index[i].get_bucket_array());
    //   }
    // }


    if(relations[0] != NULL)
      delete(relations[0]);
    if(relations[1] != NULL)
      delete(relations[1]);
    if(result != NULL)
      delete(result);
}
