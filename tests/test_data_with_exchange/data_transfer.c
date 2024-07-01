#include "data_transfer.h"

Data_Controller * init_data_controller(uint64_t max_data_links, uint64_t max_outstanding_requests, int num_data_qps) {

}

// assuming that QPs were initialized and help within data_controller -> data_qps and qp_id is an index into this
int setup_data_link(Data_Controller * data_controller, uint64_t self_id, uint64_t peer_id, uint16_t in_capacity, uint16_t out_capacity, int qp_id) {


}