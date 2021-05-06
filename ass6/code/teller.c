#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <inttypes.h>

#include "teller.h"
#include "account.h"
#include "branch.h"
#include "error.h"
#include "debug.h"

BranchID get_branch(AccountNumber accountNum) {
  Y;
  return (BranchID) (accountNum >> 32);
}

/*
 * deposit money into an account
 */
int
Teller_DoDeposit(Bank *bank, AccountNumber accountNum, AccountAmount amount)
{
  assert(amount >= 0);

  DPRINTF('t', ("Teller_DoDeposit(account 0x%"PRIx64" amount %"PRId64")\n",
                accountNum, amount));

  Account *account = Account_LookupByNumber(bank, accountNum);

  if (account == NULL) {
    return ERROR_ACCOUNT_NOT_FOUND;
  }

  sem_wait(&account->account_sema);
  BranchID cur_branch_id = get_branch(accountNum);
  sem_wait(&bank->branches[cur_branch_id].branch_sema);
  Account_Adjust(bank,account, amount, 1);
  sem_post(&account->account_sema);
  sem_post(&bank->branches[cur_branch_id].branch_sema);

  return ERROR_SUCCESS;
}

/*
 * withdraw money from an account
 */
int
Teller_DoWithdraw(Bank *bank, AccountNumber accountNum, AccountAmount amount)
{
  assert(amount >= 0);

  DPRINTF('t', ("Teller_DoWithdraw(account 0x%"PRIx64" amount %"PRId64")\n",
                accountNum, amount));

  Account *account = Account_LookupByNumber(bank, accountNum);

  if (account == NULL) {
    return ERROR_ACCOUNT_NOT_FOUND;
  }

  sem_wait(&account->account_sema);
  BranchID cur_branch_id = get_branch(accountNum);
  sem_wait(&bank->branches[cur_branch_id].branch_sema);

  if (amount > Account_Balance(account)) {
    sem_post(&account->account_sema);
    sem_post(&bank->branches[cur_branch_id].branch_sema);
    return ERROR_INSUFFICIENT_FUNDS;
  }

  Account_Adjust(bank,account, -amount, 1);
  sem_post(&account->account_sema);
  sem_post(&bank->branches[cur_branch_id].branch_sema);

  return ERROR_SUCCESS;
}

int Teller_transferSameBranches(Bank *bank, Account *srcAccount, Account *dstAccount, AccountAmount amount) {
  // lock higher account first to avoid deadlock
  if (srcAccount->accountNumber > dstAccount->accountNumber) {
    sem_wait(&srcAccount->account_sema);
    sem_wait(&dstAccount->account_sema);
  } else {
    sem_wait(&dstAccount->account_sema);
    sem_wait(&srcAccount->account_sema);
  }

  if (amount > Account_Balance(srcAccount)) {
    sem_post(&srcAccount->account_sema);
    sem_post(&dstAccount->account_sema);
    return ERROR_INSUFFICIENT_FUNDS;
  }

  Account_Adjust(bank, srcAccount, -amount, 0);
  Account_Adjust(bank, dstAccount, amount, 0);
  sem_post(&srcAccount->account_sema);
  sem_post(&dstAccount->account_sema);
  return ERROR_SUCCESS;

}

int Teller_transferDifferentBranches(Bank *bank, Account *srcAccount, Account *dstAccount,
                      AccountAmount amount, BranchID src_branch_id, BranchID dst_branch_id) {
  // for different branches we lock higher branch first to avoid deadlock
  if (src_branch_id > dst_branch_id) {
    sem_wait(&srcAccount->account_sema);
    sem_wait(&dstAccount->account_sema);
    sem_wait(&bank->branches[src_branch_id].branch_sema);
    sem_wait(&bank->branches[dst_branch_id].branch_sema);
  } else {
    sem_wait(&dstAccount->account_sema);
    sem_wait(&srcAccount->account_sema);
    sem_wait(&bank->branches[dst_branch_id].branch_sema);
    sem_wait(&bank->branches[src_branch_id].branch_sema);
  }

  if (amount > Account_Balance(srcAccount)) {
    sem_post(&srcAccount->account_sema);
    sem_post(&dstAccount->account_sema);
    sem_post(&bank->branches[src_branch_id].branch_sema);
    sem_post(&bank->branches[dst_branch_id].branch_sema); 
    return ERROR_INSUFFICIENT_FUNDS;
  }

  Account_Adjust(bank, srcAccount, -amount, 1);
  Account_Adjust(bank, dstAccount, amount, 1);
  sem_post(&srcAccount->account_sema);
  sem_post(&dstAccount->account_sema);
  sem_post(&bank->branches[src_branch_id].branch_sema);
  sem_post(&bank->branches[dst_branch_id].branch_sema); 

  return ERROR_SUCCESS;
}

/*
 * do a tranfer from one account to another account
 */
int
Teller_DoTransfer(Bank *bank, AccountNumber srcAccountNum,
                  AccountNumber dstAccountNum,
                  AccountAmount amount)
{
  assert(amount >= 0);
  DPRINTF('t', ("Teller_DoTransfer(src 0x%"PRIx64", dst 0x%"PRIx64
                ", amount %"PRId64")\n",
                srcAccountNum, dstAccountNum, amount));

  Account *srcAccount = Account_LookupByNumber(bank, srcAccountNum);
  if (srcAccount == NULL) {
    return ERROR_ACCOUNT_NOT_FOUND;
  }

  Account *dstAccount = Account_LookupByNumber(bank, dstAccountNum);
  if (dstAccount == NULL) {
    return ERROR_ACCOUNT_NOT_FOUND;
  }

  if (srcAccount==dstAccount) return ERROR_SUCCESS;
  BranchID src_branch_id = get_branch(srcAccountNum);
  BranchID dst_branch_id = get_branch(dstAccountNum);
  
  if (src_branch_id == dst_branch_id) {
    return Teller_transferSameBranches(bank, srcAccount, dstAccount, amount);
  }
  return Teller_transferDifferentBranches(bank, srcAccount, dstAccount, amount, src_branch_id, dst_branch_id);

}
