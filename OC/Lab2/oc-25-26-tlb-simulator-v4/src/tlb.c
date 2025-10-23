#include "tlb.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clock.h"
#include "constants.h"
#include "log.h"
#include "memory.h"
#include "page_table.h"

typedef struct {
  bool valid;
  bool dirty;
  uint64_t last_access;
  va_t virtual_page_number;
  pa_dram_t physical_page_number;
} tlb_entry_t;

tlb_entry_t tlb_l1[TLB_L1_SIZE];
tlb_entry_t tlb_l2[TLB_L2_SIZE];

uint64_t tlb_l1_hits = 0;
uint64_t tlb_l1_misses = 0;
uint64_t tlb_l1_invalidations = 0;

uint64_t tlb_l2_hits = 0;
uint64_t tlb_l2_misses = 0;
uint64_t tlb_l2_invalidations = 0;

uint64_t get_total_tlb_l1_hits() { return tlb_l1_hits; }
uint64_t get_total_tlb_l1_misses() { return tlb_l1_misses; }
uint64_t get_total_tlb_l1_invalidations() { return tlb_l1_invalidations; }

uint64_t get_total_tlb_l2_hits() { return tlb_l2_hits; }
uint64_t get_total_tlb_l2_misses() { return tlb_l2_misses; }
uint64_t get_total_tlb_l2_invalidations() { return tlb_l2_invalidations; }

static uint64_t access_counter = 0;

void tlb_init() {
  memset(tlb_l1, 0, sizeof(tlb_l1));
  memset(tlb_l2, 0, sizeof(tlb_l2));
  tlb_l1_hits = 0;
  tlb_l1_misses = 0;
  tlb_l1_invalidations = 0;
  tlb_l2_hits = 0;
  tlb_l2_misses = 0;
  tlb_l2_invalidations = 0;
}

/**
 * @brief Invalida uma página virtual em toda a hierarquia TLB
 * 
 * Esta função implementa invalidação com política de inclusão, procurando
 * a página virtual em ambos os níveis da hierarquia TLB:
 * - Procura primeiro na L1 (sempre paga latência L1)
 * - Procura depois na L2 (sempre paga latência L2)
 * - Invalida a entrada em ambos os níveis se necessário
 * - Faz write-back se a entrada invalidada estiver dirty
 * - Latência total = L1_LATENCY + L2_LATENCY (sempre que procura em ambos)
 * 
 * @param virtual_page_number VPN (Virtual Page Number) a ser invalidado
 */
void tlb_invalidate(va_t virtual_page_number) {
  va_t vpn = virtual_page_number;
  
  // PASSO 1: Procurar e invalidar na L1
  //increment_time(TLB_L1_LATENCY_NS);
  for (int i = 0; i < TLB_L1_SIZE; i++) {
    if (tlb_l1[i].valid && tlb_l1[i].virtual_page_number == vpn) {
      tlb_l1_invalidations++;  // Incrementar contador de invalidações L1

      // Se entrada dirty, fazer write-back antes de invalidar
      /*if (tlb_l1[i].dirty) {
        write_back_tlb_entry(tlb_l1[i].physical_page_number << PAGE_SIZE_BITS);
      }*/
      tlb_l1[i].valid = false;  // Marcar entrada como inválida
      break; // Continuar para invalidar também na L2 (política inclusiva)
    }
  }
  // PASSO 2: Procurar e invalidar na L2
  //increment_time(TLB_L2_LATENCY_NS);
  for (int i = 0; i < TLB_L2_SIZE; i++) {
    if (tlb_l2[i].valid && tlb_l2[i].virtual_page_number == vpn) {
      tlb_l2_invalidations++;  // Incrementar contador de invalidações L2

      // Se entrada dirty, fazer write-back antes de invalidar
      if (tlb_l2[i].dirty) {
        write_back_tlb_entry(tlb_l2[i].physical_page_number << PAGE_SIZE_BITS);
      }
      tlb_l2[i].valid = false;  // Marcar entrada como inválida
      break;  // VPN encontrado e invalidado, sair do loop
    }
  }
  // Se não encontrou em nenhum nível, não faz nada
}

/**
 * @brief Encontra a vítima LRU (Least Recently Used) na L1 TLB
 * 
 * Esta função implementa a política de substituição LRU para a L1 TLB.
 * Primeiro procura por entradas inválidas (espaços livres). Se não encontrar,
 * seleciona a entrada com o timestamp mais antigo (menos recentemente usada).
 * 
 * @return Índice da entrada vítima na L1 TLB (0 a TLB_L1_SIZE-1)
 */
// Função auxiliar para encontrar vítima LRU na L1
int find_lru_victim_l1() {
  int lru_index = 0;  // Índice da entrada LRU (inicialmente 0)
  uint64_t oldest_time = tlb_l1[0].last_access;  // Timestamp mais antigo encontrado
  
  // Primeiro procurar entradas inválidas (espaços livres)
  for (int i = 0; i < TLB_L1_SIZE; i++) {
    if (!tlb_l1[i].valid) {
      return i;  // Retorna imediatamente se encontrar espaço livre
    }
  }
  
  // Se todas válidas, encontrar a mais antiga (LRU)
  // Percorre todas as entradas para encontrar o menor timestamp
  for (int i = 1; i < TLB_L1_SIZE; i++) {
    if (tlb_l1[i].last_access < oldest_time) {
      oldest_time = tlb_l1[i].last_access;  // Atualiza o timestamp mais antigo
      lru_index = i;  // Atualiza o índice da vítima
    }
  }
  return lru_index;  // Retorna a posição da entrada menos recentemente usada
}

/**
 * @brief Encontra a vítima LRU (Least Recently Used) na L2 TLB
 * 
 * Esta função implementa a política de substituição LRU para a L2 TLB.
 * Funciona de forma idêntica à find_lru_victim_l1
 * Primeiro procura espaços livres, depois seleciona a entrada LRU.
 * 
 * @return Índice da entrada vítima na L2 TLB (0 a TLB_L2_SIZE-1)
 */
// Função auxiliar para encontrar vítima LRU na L2
int find_lru_victim_l2() {
  int lru_index = 0;  // Índice da entrada LRU (inicialmente 0)
  uint64_t oldest_time = tlb_l2[0].last_access;  // Timestamp mais antigo encontrado
  
  // Primeiro procurar entradas inválidas (espaços livres)
  for (int i = 0; i < TLB_L2_SIZE; i++) {
    if (!tlb_l2[i].valid) {
      return i;  // Retorna imediatamente se encontrar espaço livre
    }
  }
  // Se todas válidas, encontrar a mais antiga (LRU)
  for (int i = 1; i < TLB_L2_SIZE; i++) {
    if (tlb_l2[i].last_access < oldest_time) {
      oldest_time = tlb_l2[i].last_access;  // Atualiza o timestamp mais antigo
      lru_index = i;  // Atualiza o índice da vítima
    }
  }
  return lru_index;  // Retorna a posição da entrada menos recentemente usada
}

/**
 * @brief Traduz endereço virtual para físico usando hierarquia TLB L1/L2
 * 
 * Esta função implementa uma hierarquia TLB de 2 níveis com política de inclusão:
 * - L1 TLB
 * - L2 TLB
 * - Page Table
 * 
 * Fluxo de tradução:
 * 1. L1 hit → retorna imediatamente (caso mais comum)
 * 2. L1 miss, L2 hit → promove para L1, pode expulsar vítima L1 para L2
 * 3. L1 miss, L2 miss → page fault, busca na page table, insere em L1+L2
 * 
 * @param virtual_address Endereço virtual completo a ser traduzido
 * @param op Tipo de operação (OP_READ ou OP_WRITE) para marcar dirty bit
 * @return Endereço físico correspondente na DRAM
 */
pa_dram_t tlb_translate(va_t virtual_address, op_t op) {
  // Extrair VPN e page offset do endereço virtual
  va_t vpn = virtual_address >> PAGE_SIZE_BITS;
  pa_dram_t page_offset = virtual_address & ((1ULL << PAGE_SIZE_BITS) - 1);
  
  // PASSO 1: Procurar na L1 TLB (caso mais frequente)
  for (int i = 0; i < TLB_L1_SIZE; i++) {
    if (tlb_l1[i].valid && tlb_l1[i].virtual_page_number == vpn) {
      tlb_l1_hits++;  // Incrementar contador de hits L1
      tlb_l1[i].last_access = ++access_counter;  // Atualizar timestamp LRU
      increment_time(TLB_L1_LATENCY_NS); 
      
      // Marcar como dirty se for operação de escrita
      if (op == OP_WRITE) {
        tlb_l1[i].dirty = true;
      }
      // Reconstruir endereço físico: PPN + offset
      pa_dram_t result = (tlb_l1[i].physical_page_number << PAGE_SIZE_BITS) | page_offset;

      return result;  // Retorno rápido, operação completa
    }
  }
  
  // MISS na L1 - procurar na L2
  tlb_l1_misses++;  // Incrementar contador de misses L1
  increment_time(TLB_L1_LATENCY_NS); 

  // PASSO 2: Procurar na L2 TLB
  for (int i = 0; i < TLB_L2_SIZE; i++) {
    if (tlb_l2[i].valid && tlb_l2[i].virtual_page_number == vpn) {
      tlb_l2_hits++;  // Incrementar contador de hits L2
      tlb_l2[i].last_access = ++access_counter;  // Atualizar timestamp LRU
      increment_time(TLB_L2_LATENCY_NS); 

      // Marcar como dirty se for operação de escrita
      if (op == OP_WRITE) {
        tlb_l2[i].dirty = true;
      }
 
      // Promover entrada da L2 para L1 (política de inclusão)
      int l1_pos = find_lru_victim_l1();  // Encontrar posição na L1

      // Se vítima da L1 for dirty e válida, mover para L2
      // Isto mantém a política de inclusão (L1 ⊆ L2)
      if (tlb_l1[l1_pos].valid && tlb_l1[l1_pos].dirty) {
        int l2_pos = find_lru_victim_l2();  // Encontrar posição na L2
        
        // EVITAR sobrescrever a entrada que acabou de fazer hit na L2
        if (l2_pos == i) {	
          uint64_t oldest_time = tlb_l2[0].last_access;
          // Procurar próxima vítima LRU que não seja a entrada atual
          for (int j = 1; j < TLB_L2_SIZE; j++) {
            if (tlb_l2[j].last_access < oldest_time) {
              oldest_time = tlb_l2[j].last_access;
              if (j != i) {
                l2_pos = j;
              }
            }
          }
        }
        tlb_l2[l2_pos] = tlb_l1[l1_pos];  // Mover vítima L1 para L2
      }
      
      // Promover entrada da L2 para L1 (mantendo inclusão L1 ⊆ L2)
      tlb_l1[l1_pos] = tlb_l2[i];
      // Atualizar também o timestamp da L2 para manter coerência temporal
      tlb_l2[i].last_access = access_counter;

      // Reconstruir endereço físico: PPN + offset
      pa_dram_t result = (tlb_l2[i].physical_page_number << PAGE_SIZE_BITS) | page_offset;
      
      return result;  // Tradução completa via L2
    }
  }
  
  // MISS na L2 
  tlb_l2_misses++;  // Incrementar contador de misses L2
  increment_time(TLB_L2_LATENCY_NS);  
  
  // PASSO 3: Procurar na Page Table (operação mais custosa)
  pa_dram_t physical_address = page_table_translate(virtual_address, op);
  pa_dram_t ppn = physical_address >> PAGE_SIZE_BITS;  // Extrair PPN
  
  // Inserir resultado na L2 primeiro (política de inclusão)
  int l2_pos = find_lru_victim_l2();
  // Se a vítima da L2 for dirty, fazer write-back (sai da hierarquia)
  if (tlb_l2[l2_pos].valid && tlb_l2[l2_pos].dirty) {
    write_back_tlb_entry(tlb_l2[l2_pos].physical_page_number << PAGE_SIZE_BITS);
  }
  // Inserir nova entrada na L2
  tlb_l2[l2_pos].valid = true;
  tlb_l2[l2_pos].virtual_page_number = vpn;
  tlb_l2[l2_pos].physical_page_number = ppn;
  tlb_l2[l2_pos].dirty = (op == OP_WRITE);  // Dirty apenas se for write
  tlb_l2[l2_pos].last_access = ++access_counter;
  
  // Encontrar posição de L1
  int l1_pos = find_lru_victim_l1();

  // Inserir também na L1 (política de inclusão)
  tlb_l1[l1_pos].valid = true;
  tlb_l1[l1_pos].virtual_page_number = vpn;
  tlb_l1[l1_pos].physical_page_number = ppn;
  tlb_l1[l1_pos].dirty = (op == OP_WRITE);  // Dirty apenas se for write
  tlb_l1[l1_pos].last_access = ++access_counter;  // Mesmo timestamp da L2
  
  return physical_address;  // Tradução completa via page table
}
