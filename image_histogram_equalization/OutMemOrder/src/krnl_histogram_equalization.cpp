#include <iostream>
#include <vector>
#include <numeric>
#include <cassert>
#include <cmath>

static const double one { 1 }; 
static const double eps { 1e-8 }; 

#define VERBOSE 0 // turn this on in sw_emu if you want to see the intermediate results
#define DEBUG 0 // turn this on in sw_emu if logic is not working as expected
#define BINS_NUM 256

void cal_acc_hist(double* freq, double* ret) {
    
    double acc {}; 

    for (uint32_t i = 0; i < BINS_NUM; ++i) {
        // HLS pragma to pipeline the loop with initiation interval of 1
        #pragma HLS PIPELINE II=1
        acc += freq[i]; 
        ret[i] = acc; 
    }

    assert(abs(acc - one) < eps); 

}

void cal_round(double* acc_hist, size_t* ret) {

    for (uint32_t i = 0; i < BINS_NUM; ++i) {
        // HLS pragma to pipeline the loop with initiation interval of 1
        #pragma HLS PIPELINE II=1
        ret[i] = static_cast<size_t>((BINS_NUM - 1) * acc_hist[i] + 0.5); 
    }

}

void cal_new_freq(double* freq, size_t* round, double* ret) {

    for (uint32_t i = 0; i < BINS_NUM; ++i) {
        // HLS pragma to pipeline the loop with initiation interval of 1
        #pragma HLS PIPELINE II=1
        ret[round[i]] += freq[i]; 
        
    }

}

extern "C" {

    void krnl_histogram_equalization(uint8_t *ptr_plainText, int inputLength, double *freq_cipherText) {

        if (VERBOSE && DEBUG){
            printf("inputSize: %d\n", inputLength);
            for (int i = 0; i < inputLength; i++) {
                std::cout<<ptr_plainText[i];
            }
            printf("\n");
        }

        if (VERBOSE && DEBUG){
            std::cout << "freq_cipherText to krnl\t\t"; 
            for (size_t i = 0; i < BINS_NUM; ++i) {
                std::cout << freq_cipherText[i] << "\t";
            }
            std::cout << std::endl;
        }

        double freq_plainText[BINS_NUM] = {0};

        for(uint32_t i = 0; i < inputLength; i++){
            // HLS pragma to pipeline the loop with initiation interval of 1
            #pragma HLS PIPELINE II=1
            assert(ptr_plainText[i] < BINS_NUM && ptr_plainText[i] >= 0); 
            freq_plainText[ptr_plainText[i]] += 1;
        }


        for(uint32_t i = 0; i < BINS_NUM; i++){
            // HLS pragma to pipeline the loop with initiation interval of 1
            #pragma HLS PIPELINE II=1
            freq_plainText[i] /= (double)inputLength;
            // Initialize the output
            freq_cipherText[i] = 0;
        }

        if (VERBOSE & DEBUG){
            std::cout << "origin_hist\t\t"; 
            for (size_t i = 0; i < BINS_NUM; ++i) {
                if(freq_plainText[i] != 0)
                    std::cout << "Index: " << i << ", Element: " << freq_plainText[i] << "\t";
            }
            std::cout << std::endl;
        }
        
        // Calculate
        
        double acc_hist[BINS_NUM] = {0}; 
        cal_acc_hist(freq_plainText, acc_hist);
        if (VERBOSE){
            std::cout << "acc_hist\t\t"; 
            for (double elem : acc_hist) std::cout << elem << "\t";
            std::cout << std::endl; 
        }

        if(VERBOSE && DEBUG){
            std::cout << "origin_hist after cal_acc_hist:\n"; 
            for (size_t i = 0; i < BINS_NUM; ++i) {
                std::cout << freq_plainText[i] << "\t";
            }
            std::cout << std::endl;
        }

        size_t round[BINS_NUM] = {0}; 
        cal_round(acc_hist, round);
        if (VERBOSE){
            std::cout << "round\t\t\t"; 
            for (size_t elem : round) std::cout << elem << "\t";
            std::cout << std::endl; 
        }

        if(VERBOSE && DEBUG){
            std::cout << "origin_hist after cal_round:\n"; 
            for (size_t i = 0; i < BINS_NUM; ++i) {
                std::cout << freq_plainText[i] << "\t";
            }
            std::cout << std::endl;
        }

        cal_new_freq(freq_plainText, round, freq_cipherText);

        if (VERBOSE){
            std::cout << "new_freq\t\t"; 
            for(size_t i=0; i<BINS_NUM; i++){
                std::cout << freq_cipherText[i] << "\t";
            }
            std::cout << std::endl; 
        }
    
    }
}

// int main() {
    
//     double plainText[] = {0.1, 0.2, 0.3, 0.4};
//     double cipherText[4];
//     histogram_equalization(plainText, 4, cipherText);

//     return 0; 
// }
