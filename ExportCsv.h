// The core-owned CSV export of the full core state (the user's data
// archive): ONE consolidated, canonical file - the complete transaction
// table of every wallet (the crypto inner, outside and card maps) plus
// the user-owned balance-sheet section plus a trailing export stamp.
//
// The state's owner builds the dump from its OWN state, so the wallet
// names, the wallet-kind partition, the transaction vocabulary and the
// free text are inherited verbatim (faithful by construction - an
// app-side serializer could only mirror them and drift). The app
// transports the resulting bytes and owns no part of the format.
//
// File contract (byte-frozen by the core-suite fixture):
//   line 1        the exact header (kHeaderLine)
//   one row each  every transaction of every wallet, sorted by wallet
//                 name, then transaction date (exact ties keep the
//                 wallet's stored row order)
//   one marker    kBalanceSheetMarker (always, before the sheet lines)
//   zero or more  the balance-sheet lines - the sheet's own line serde,
//                 every collection in stored order (the L lines carry
//                 the core-computed ;bal; trailer AT THE EXPORT TIME)
//   one trailer   "# exported <YYYY-MM-DDTHH:MM:SS> by cdcsvparser"
//                 (the wall-clock local time of the export; the file's
//                 only metadata - the timestamp VALUE is the only line
//                 that is not byte-stable between two exports)
// Every line - including the last - ends with '\n'. The single escape
// rule of the sheet serde (';' -> ';;') applies to every free-text
// column (wallet, tx_kind, description).

#ifndef NF_TX_CORE_EXPORTCSV_H
#define NF_TX_CORE_EXPORTCSV_H

#include <ctime>
#include <string>

class BalanceSheet;
class TransactionManager;

namespace CsvExport {

//! The exact 9-column header line (part of the byte contract).
extern const char *kHeaderLine;
//! The balance-sheet section marker line (part of the byte contract).
extern const char *kBalanceSheetMarker;

//! The whole export file of the state of [tm] + [sheet] as of [now]:
//! [now] prices BOTH the liability balances (the ;bal; trailer) and the
//! trailing stamp, so the file is self-consistent at one instant.
std::string buildCsvFile(TransactionManager &tm, const BalanceSheet &sheet,
                         const std::tm &now);

} // namespace CsvExport

#endif //NF_TX_CORE_EXPORTCSV_H
