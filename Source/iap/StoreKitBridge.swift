#if os(iOS)

import Foundation
import StoreKit
import UIKit

public typealias StoreKitCallback = @convention(c) (Int32, UnsafePointer<CChar>?, UnsafePointer<CChar>?) -> Void

private enum BridgeState: Int32
{
    case unavailable = 0
    case loading = 1
    case ready = 2
    case purchasing = 3
    case restoring = 4
    case unlocked = 5
    case pending = 6
    case failed = 7
}

private enum EntitlementSource: String, Codable
{
    case inAppPurchase
    case legacyPaidApp
}

private struct EntitlementProjection: Codable
{
    let schemaVersion: Int
    let unlocked: Bool
    let source: EntitlementSource?
    let productID: String
    let appBundleID: String
    let transactionID: String
    let originalTransactionID: String
    let environment: String
    let purchaseDate: Double
    let originalAppVersion: String
    let verifiedAt: Double
    let signedTransactionInfo: String
}

private enum SharedEntitlement
{
    static let schemaVersion = 2
    private static let directoryName = "SoundRave"
    private static let fileName = "storekit2-entitlement-v1.json"

    static func containerIsAvailable(appGroupID: String) -> Bool
    {
        return FileManager.default.containerURL(
            forSecurityApplicationGroupIdentifier: appGroupID) != nil
    }

    static func isUnlocked(productID: String,
                           appBundleID: String,
                           appGroupID: String,
                           legacyPaidThroughVersion: String) -> Bool
    {
        guard let fileURL = fileURL(appGroupID: appGroupID),
              let data = try? Data(contentsOf: fileURL),
              data.count <= 64 * 1024,
              let projection = try? JSONDecoder().decode(EntitlementProjection.self, from: data)
        else
        {
            return false
        }

        let jwsParts = projection.signedTransactionInfo.split(
            separator: ".", omittingEmptySubsequences: false)
        let commonFieldsAreValid = projection.schemaVersion == schemaVersion
            && projection.unlocked
            && projection.productID == productID
            && projection.appBundleID == appBundleID
            && !projection.transactionID.isEmpty
            && !projection.originalTransactionID.isEmpty
            && !projection.environment.isEmpty
            && projection.purchaseDate > 0
            && projection.verifiedAt > 0
            && jwsParts.count == 3
            && jwsParts.allSatisfy { !$0.isEmpty }

        guard commonFieldsAreValid else
        {
            return false
        }

        switch projection.source
        {
            case .inAppPurchase:
                return projection.originalAppVersion.isEmpty

            case .legacyPaidApp:
                return projection.transactionID == projection.originalTransactionID
                    && legacyEnvironmentIsAllowed(projection.environment)
                    && version(projection.originalAppVersion,
                               isAtMost: legacyPaidThroughVersion)

            case .none:
                return false
        }
    }

    static func store(transaction: Transaction,
                      signedTransactionInfo: String,
                      appGroupID: String) throws
    {
        guard let fileURL = fileURL(appGroupID: appGroupID) else
        {
            throw CocoaError(.fileNoSuchFile)
        }

        try FileManager.default.createDirectory(
            at: fileURL.deletingLastPathComponent(),
            withIntermediateDirectories: true)

        let projection = EntitlementProjection(
            schemaVersion: schemaVersion,
            unlocked: true,
            source: .inAppPurchase,
            productID: transaction.productID,
            appBundleID: transaction.appBundleID,
            transactionID: String(transaction.id),
            originalTransactionID: String(transaction.originalID),
            environment: environmentName(for: transaction),
            purchaseDate: transaction.purchaseDate.timeIntervalSince1970,
            originalAppVersion: "",
            verifiedAt: Date().timeIntervalSince1970,
            signedTransactionInfo: signedTransactionInfo)
        try write(projection, to: fileURL)
    }

    static func store(appTransaction: AppTransaction,
                      signedTransactionInfo: String,
                      productID: String,
                      appGroupID: String) throws
    {
        guard let fileURL = fileURL(appGroupID: appGroupID) else
        {
            throw CocoaError(.fileNoSuchFile)
        }

        let appTransactionID = appTransaction.appTransactionID
        let projection = EntitlementProjection(
            schemaVersion: schemaVersion,
            unlocked: true,
            source: .legacyPaidApp,
            productID: productID,
            appBundleID: appTransaction.bundleID,
            transactionID: appTransactionID,
            originalTransactionID: appTransactionID,
            environment: appTransaction.environment.rawValue,
            purchaseDate: appTransaction.originalPurchaseDate.timeIntervalSince1970,
            originalAppVersion: appTransaction.originalAppVersion,
            verifiedAt: Date().timeIntervalSince1970,
            signedTransactionInfo: signedTransactionInfo)
        try write(projection, to: fileURL)
    }

    private static func write(_ projection: EntitlementProjection,
                              to fileURL: URL) throws
    {
        try FileManager.default.createDirectory(
            at: fileURL.deletingLastPathComponent(),
            withIntermediateDirectories: true)
        let data = try JSONEncoder().encode(projection)
        try data.write(to: fileURL, options: .atomic)
        try FileManager.default.setAttributes(
            [.protectionKey: FileProtectionType.completeUntilFirstUserAuthentication],
            ofItemAtPath: fileURL.path)
    }

    static func clear(appGroupID: String)
    {
        guard let fileURL = fileURL(appGroupID: appGroupID) else
        {
            return
        }

        let lockedProjection = EntitlementProjection(
            schemaVersion: schemaVersion,
            unlocked: false,
            source: nil,
            productID: "",
            appBundleID: "",
            transactionID: "",
            originalTransactionID: "",
            environment: "",
            purchaseDate: 0,
            originalAppVersion: "",
            verifiedAt: Date().timeIntervalSince1970,
            signedTransactionInfo: "")

        do
        {
            try FileManager.default.createDirectory(
                at: fileURL.deletingLastPathComponent(),
                withIntermediateDirectories: true)
            try JSONEncoder().encode(lockedProjection).write(to: fileURL, options: .atomic)
            try FileManager.default.setAttributes(
                [.protectionKey: FileProtectionType.completeUntilFirstUserAuthentication],
                ofItemAtPath: fileURL.path)
        }
        catch
        {
            try? FileManager.default.removeItem(at: fileURL)
        }
    }

    private static func fileURL(appGroupID: String) -> URL?
    {
        return FileManager.default.containerURL(
            forSecurityApplicationGroupIdentifier: appGroupID)?
            .appendingPathComponent(directoryName, isDirectory: true)
            .appendingPathComponent(fileName, isDirectory: false)
    }

    private static func environmentName(for transaction: Transaction) -> String
    {
        return transaction.environment.rawValue
    }

    static func version(_ candidate: String, isAtMost cutoff: String) -> Bool
    {
        guard let candidateComponents = versionComponents(candidate),
              let cutoffComponents = versionComponents(cutoff)
        else
        {
            return false
        }

        return candidateComponents.lexicographicallyPrecedes(cutoffComponents)
            || candidateComponents == cutoffComponents
    }

    private static func versionComponents(_ value: String) -> [Int]?
    {
        let rawComponents = value.split(separator: ".", omittingEmptySubsequences: false)
        guard !rawComponents.isEmpty, rawComponents.count <= 3 else
        {
            return nil
        }

        var components: [Int] = []
        for rawComponent in rawComponents
        {
            guard !rawComponent.isEmpty,
                  rawComponent.count <= 9,
                  rawComponent.allSatisfy({ $0.isASCII && $0.isNumber }),
                  let component = Int(rawComponent)
            else
            {
                return nil
            }

            components.append(component)
        }

        while components.count < 3
        {
            components.append(0)
        }

        return components
    }

    private static func legacyEnvironmentIsAllowed(_ environment: String) -> Bool
    {
        if environment == AppStore.Environment.production.rawValue
        {
            return true
        }

#if DEBUG
        return environment == AppStore.Environment.sandbox.rawValue
            || environment == AppStore.Environment.xcode.rawValue
#else
        return false
#endif
    }
}

private struct DemoAccessProjection: Codable
{
    let schemaVersion: Int
    let productID: String
    let appBundleID: String
    let sessionStartedAt: Double
    let sessionEndsAt: Double
    var nextEligibleAt: Double
    var sessionConsumed: Bool
    var activeSecondsRemaining: Double?
    var lastObservedWallTime: Double?
}

private enum DemoAccessState: Int32
{
    case unavailable = -1
    case locked = 0
    case allowed = 1
}

private struct DemoAccessSnapshot
{
    let state: DemoAccessState
    let secondsRemaining: Int64
}

private enum DemoAccess
{
    private static let schemaVersion = 1
    private static let sessionDuration: TimeInterval = 10 * 60
    private static let lockDuration: TimeInterval = 24 * 60 * 60
    private static let maximumActiveTick: TimeInterval = 2.0
    private static let clockRollbackTolerance: TimeInterval = 5.0
    private static let directoryName = "SoundRave"
    private static let fileName = "storekit2-demo-access-v1.json"
    private static var activeProductKey = ""
    private static var lastActiveUptime: TimeInterval?
    private static var lifecycleObservers: [NSObjectProtocol] = []

    static func evaluate(productID: String,
                         appBundleID: String,
                         appGroupID: String,
                         now: TimeInterval = Date().timeIntervalSince1970)
        -> DemoAccessSnapshot
    {
        guard now.isFinite, now > 0,
              let fileURL = fileURL(appGroupID: appGroupID)
        else
        {
            return DemoAccessSnapshot(state: .unavailable, secondsRemaining: 0)
        }

        ensureLifecycleObservation()
        let activelyUsed = isActivelyUsed()

        if !FileManager.default.fileExists(atPath: fileURL.path)
        {
            return startSession(productID: productID,
                                appBundleID: appBundleID,
                                fileURL: fileURL,
                                now: now,
                                activelyUsed: activelyUsed)
        }

        guard let data = try? Data(contentsOf: fileURL),
              data.count <= 64 * 1024,
              var projection = try? JSONDecoder().decode(
                DemoAccessProjection.self, from: data),
              isValid(projection,
                      productID: productID,
                      appBundleID: appBundleID)
        else
        {
            return DemoAccessSnapshot(state: .unavailable, secondsRemaining: 0)
        }

        let lastObservedWallTime = projection.lastObservedWallTime
            ?? projection.sessionStartedAt
        if now + clockRollbackTolerance < lastObservedWallTime
        {
            resetActiveClock()
            projection.sessionConsumed = true
            projection.activeSecondsRemaining = 0
            projection.nextEligibleAt = max(
                projection.nextEligibleAt,
                lastObservedWallTime + lockDuration)
            projection.lastObservedWallTime = lastObservedWallTime
            guard store(projection, to: fileURL) else
            {
                return DemoAccessSnapshot(state: .unavailable, secondsRemaining: 0)
            }

            return DemoAccessSnapshot(
                state: .locked,
                secondsRemaining: seconds(until: projection.nextEligibleAt, now: now))
        }

        if projection.sessionConsumed
        {
            resetActiveClock()
            if now >= projection.nextEligibleAt
            {
                return startSession(productID: productID,
                                    appBundleID: appBundleID,
                                    fileURL: fileURL,
                                    now: now,
                                    activelyUsed: activelyUsed)
            }

            return DemoAccessSnapshot(
                state: .locked,
                secondsRemaining: seconds(until: projection.nextEligibleAt, now: now))
        }

        var remaining = projection.activeSecondsRemaining ?? sessionDuration
        guard remaining.isFinite, remaining >= 0, remaining <= sessionDuration else
        {
            return DemoAccessSnapshot(state: .unavailable, secondsRemaining: 0)
        }

        remaining = max(0, remaining - activeElapsed(
            productID: productID,
            appBundleID: appBundleID,
            activelyUsed: activelyUsed))
        projection.activeSecondsRemaining = remaining

        if !activelyUsed
        {
            return DemoAccessSnapshot(
                state: .allowed,
                secondsRemaining: seconds(remaining: remaining))
        }

        projection.lastObservedWallTime = max(lastObservedWallTime, now)
        if remaining <= 0
        {
            resetActiveClock()
            projection.sessionConsumed = true
            projection.nextEligibleAt = now + lockDuration
        }

        guard store(projection, to: fileURL) else
        {
            return DemoAccessSnapshot(state: .unavailable, secondsRemaining: 0)
        }

        if projection.sessionConsumed
        {
            return DemoAccessSnapshot(
                state: .locked,
                secondsRemaining: seconds(until: projection.nextEligibleAt, now: now))
        }

        return DemoAccessSnapshot(
            state: .allowed,
            secondsRemaining: seconds(remaining: remaining))
    }

    private static func startSession(productID: String,
                                     appBundleID: String,
                                     fileURL: URL,
                                     now: TimeInterval,
                                     activelyUsed: Bool) -> DemoAccessSnapshot
    {
        let sessionEndsAt = now + sessionDuration
        let projection = DemoAccessProjection(
            schemaVersion: schemaVersion,
            productID: productID,
            appBundleID: appBundleID,
            sessionStartedAt: now,
            sessionEndsAt: sessionEndsAt,
            nextEligibleAt: sessionEndsAt + lockDuration,
            sessionConsumed: false,
            activeSecondsRemaining: sessionDuration,
            lastObservedWallTime: now)

        guard store(projection, to: fileURL) else
        {
            return DemoAccessSnapshot(state: .unavailable, secondsRemaining: 0)
        }

        if activelyUsed
        {
            beginActiveClock(productID: productID, appBundleID: appBundleID)
        }
        else
        {
            resetActiveClock()
        }

        return DemoAccessSnapshot(state: .allowed,
                                  secondsRemaining: Int64(sessionDuration))
    }

    private static func isValid(_ projection: DemoAccessProjection,
                                productID: String,
                                appBundleID: String) -> Bool
    {
        return projection.schemaVersion == schemaVersion
            && projection.productID == productID
            && projection.appBundleID == appBundleID
            && projection.sessionStartedAt.isFinite
            && projection.sessionEndsAt.isFinite
            && projection.nextEligibleAt.isFinite
            && projection.sessionStartedAt > 0
            && projection.nextEligibleAt > 0
            && abs((projection.sessionEndsAt - projection.sessionStartedAt)
                   - sessionDuration) < 0.001
            && (projection.activeSecondsRemaining.map {
                $0.isFinite && $0 >= 0 && $0 <= sessionDuration
            } ?? true)
            && (projection.lastObservedWallTime.map {
                $0.isFinite && $0 > 0
            } ?? true)
    }

    private static func store(_ projection: DemoAccessProjection,
                              to fileURL: URL) -> Bool
    {
        do
        {
            try FileManager.default.createDirectory(
                at: fileURL.deletingLastPathComponent(),
                withIntermediateDirectories: true)
            try JSONEncoder().encode(projection).write(to: fileURL, options: .atomic)
            try FileManager.default.setAttributes(
                [.protectionKey: FileProtectionType.completeUntilFirstUserAuthentication],
                ofItemAtPath: fileURL.path)
            return true
        }
        catch
        {
            return false
        }
    }

    private static func seconds(until target: TimeInterval,
                                now: TimeInterval) -> Int64
    {
        let value = ceil(max(0, target - now))
        guard value.isFinite, value <= Double(Int64.max) else
        {
            return Int64.max
        }
        return Int64(value)
    }

    private static func seconds(remaining: TimeInterval) -> Int64
    {
        let value = ceil(max(0, remaining))
        guard value.isFinite, value <= Double(Int64.max) else
        {
            return Int64.max
        }
        return Int64(value)
    }

    private static func isActivelyUsed() -> Bool
    {
        if Bundle.main.bundleURL.pathExtension.lowercased() == "appex"
        {
            return true
        }

        return UIApplication.shared.applicationState == .active
    }

    private static func ensureLifecycleObservation()
    {
        guard Bundle.main.bundleURL.pathExtension.lowercased() != "appex",
              lifecycleObservers.isEmpty
        else
        {
            return
        }

        let center = NotificationCenter.default
        lifecycleObservers.append(center.addObserver(
            forName: UIApplication.willResignActiveNotification,
            object: nil,
            queue: .main)
        { _ in
            resetActiveClock()
        })
        lifecycleObservers.append(center.addObserver(
            forName: UIApplication.didBecomeActiveNotification,
            object: nil,
            queue: .main)
        { _ in
            resetActiveClock()
        })
    }

    private static func activeElapsed(productID: String,
                                      appBundleID: String,
                                      activelyUsed: Bool) -> TimeInterval
    {
        guard activelyUsed else
        {
            resetActiveClock()
            return 0
        }

        let key = productID + "|" + appBundleID
        let uptime = ProcessInfo.processInfo.systemUptime
        defer
        {
            activeProductKey = key
            lastActiveUptime = uptime
        }

        guard activeProductKey == key,
              let previous = lastActiveUptime,
              uptime >= previous
        else
        {
            return 0
        }

        return min(uptime - previous, maximumActiveTick)
    }

    private static func beginActiveClock(productID: String,
                                         appBundleID: String)
    {
        activeProductKey = productID + "|" + appBundleID
        lastActiveUptime = ProcessInfo.processInfo.systemUptime
    }

    private static func resetActiveClock()
    {
        activeProductKey = ""
        lastActiveUptime = nil
    }

    private static func fileURL(appGroupID: String) -> URL?
    {
        return FileManager.default.containerURL(
            forSecurityApplicationGroupIdentifier: appGroupID)?
            .appendingPathComponent(directoryName, isDirectory: true)
            .appendingPathComponent(fileName, isDirectory: false)
    }
}

@MainActor
private final class StoreKitCoordinator
{
    private enum LegacyEntitlementResult
    {
        case granted
        case notEligible
        case unavailable
    }

    static let shared = StoreKitCoordinator()

    private var callback: StoreKitCallback?
    private var productID = ""
    private var appBundleID = ""
    private var appGroupID = ""
    private var legacyPaidThroughVersion = ""
    private var product: Product?
    private var listenerTask: Task<Void, Never>?
    private var operationInFlight = false
    private var hasEntitlement = false
    private var isConfiguredForStandalone = false

    private init() {}

    func configure(productID: String,
                   appBundleID: String,
                   appGroupID: String,
                   legacyPaidThroughVersion: String,
                   callback: StoreKitCallback?)
    {
        self.callback = callback
        isConfiguredForStandalone = false

        guard Bundle.main.bundleURL.pathExtension.lowercased() != "appex",
              Bundle.main.bundleIdentifier == appBundleID,
              !legacyPaidThroughVersion.isEmpty,
              SharedEntitlement.containerIsAvailable(appGroupID: appGroupID)
        else
        {
            publish(.unavailable, message: "The store configuration is unavailable.")
            return
        }

        isConfiguredForStandalone = true

        if self.productID == productID,
           self.appBundleID == appBundleID,
           self.appGroupID == appGroupID,
           self.legacyPaidThroughVersion == legacyPaidThroughVersion,
           listenerTask != nil
        {
            Task { await refreshEntitlement() }
            return
        }

        listenerTask?.cancel()
        self.productID = productID
        self.appBundleID = appBundleID
        self.appGroupID = appGroupID
        self.legacyPaidThroughVersion = legacyPaidThroughVersion
        product = nil
        operationInFlight = false
        hasEntitlement = SharedEntitlement.isUnlocked(
            productID: productID,
            appBundleID: appBundleID,
            appGroupID: appGroupID,
            legacyPaidThroughVersion: legacyPaidThroughVersion)

        publish(hasEntitlement ? .unlocked : .loading)
        startTransactionListener()

        Task
        {
            await refreshEntitlement(publishReadyWhenMissing: false)
            await loadProduct()
        }
    }

    func purchase()
    {
        guard isConfiguredForStandalone,
              !operationInFlight,
              let product
        else
        {
            return
        }

        operationInFlight = true
        publish(.purchasing, message: "Completing purchase...")

        Task
        {
            defer { operationInFlight = false }

            do
            {
                switch try await purchaseProduct(product)
                {
                    case .success(let verification):
                        guard let verified = validated(verification) else
                        {
                            publish(.failed, message: "The App Store transaction could not be verified.")
                            return
                        }

                        grant(verified.transaction,
                              signedTransactionInfo: verified.signedTransactionInfo)
                        await verified.transaction.finish()

                    case .pending:
                        publish(.pending, message: "Purchase pending approval.")

                    case .userCancelled:
                        publish(hasEntitlement ? .unlocked : .ready,
                                message: hasEntitlement ? "" : "Purchase cancelled.")

                    @unknown default:
                        publish(.failed, message: "The App Store returned an unknown purchase result.")
                }
            }
            catch
            {
                publish(.failed, message: "The purchase could not be completed.")
            }
        }
    }

    private func purchaseProduct(_ product: Product) async throws -> Product.PurchaseResult
    {
        if #available(iOS 17.0, *), let scene = activeWindowScene()
        {
            return try await product.purchase(confirmIn: scene)
        }

        return try await product.purchase()
    }

    private func activeWindowScene() -> UIWindowScene?
    {
        let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
        return scenes.first(where: { $0.activationState == .foregroundActive }) ?? scenes.first
    }

    func restore()
    {
        guard isConfiguredForStandalone, !operationInFlight else
        {
            return
        }

        operationInFlight = true
        publish(.restoring, message: "Restoring purchases...")

        Task
        {
            defer { operationInFlight = false }

            do
            {
                try await AppStore.sync()
                await refreshEntitlement(
                    missingMessage: "No purchase was found for this product.",
                    forceAppTransactionRefresh: true)
            }
            catch
            {
                publish(.failed, message: "Purchases could not be restored.")
            }
        }
    }

    func refreshEntitlement(missingMessage: String = "",
                            publishReadyWhenMissing: Bool = true,
                            forceAppTransactionRefresh: Bool = false) async
    {
        guard isConfiguredForStandalone, !productID.isEmpty else
        {
            return
        }

        let entitlements: Transaction.Transactions
        if #available(iOS 18.4, *)
        {
            entitlements = Transaction.currentEntitlements(for: productID)
        }
        else
        {
            entitlements = Transaction.currentEntitlements
        }

        for await verification in entitlements
        {
            if let verified = validated(verification)
            {
                grant(verified.transaction,
                      signedTransactionInfo: verified.signedTransactionInfo)
                return
            }
        }

        switch await refreshLegacyPaidAppEntitlement(
            forceRefresh: forceAppTransactionRefresh)
        {
            case .granted:
                return

            case .notEligible:
                hasEntitlement = false
                SharedEntitlement.clear(appGroupID: appGroupID)
                publish(publishReadyWhenMissing ? .ready : .loading,
                        message: missingMessage)

            case .unavailable:
                hasEntitlement = SharedEntitlement.isUnlocked(
                    productID: productID,
                    appBundleID: appBundleID,
                    appGroupID: appGroupID,
                    legacyPaidThroughVersion: legacyPaidThroughVersion)
                publish(hasEntitlement ? .unlocked
                                       : publishReadyWhenMissing ? .ready : .loading,
                        message: hasEntitlement ? "" : missingMessage)
        }
    }

    private func refreshLegacyPaidAppEntitlement(
        forceRefresh: Bool) async -> LegacyEntitlementResult
    {
        let verification: VerificationResult<AppTransaction>

        do
        {
            verification = forceRefresh
                ? try await AppTransaction.refresh()
                : try await AppTransaction.shared
        }
        catch
        {
            return .unavailable
        }

        guard case .verified(let appTransaction) = verification else
        {
            return .unavailable
        }

        guard appTransaction.bundleID == appBundleID,
              !appTransaction.appTransactionID.isEmpty,
              appTransaction.originalPurchaseDate.timeIntervalSince1970 > 0,
              legacyEnvironmentCanGrant(appTransaction.environment),
              SharedEntitlement.version(
                appTransaction.originalAppVersion,
                isAtMost: legacyPaidThroughVersion)
        else
        {
            return .notEligible
        }

        let signedTransactionInfo = verification.jwsRepresentation
        let parts = signedTransactionInfo.split(
            separator: ".", omittingEmptySubsequences: false)
        guard parts.count == 3, parts.allSatisfy({ !$0.isEmpty }) else
        {
            return .unavailable
        }

        do
        {
            try SharedEntitlement.store(
                appTransaction: appTransaction,
                signedTransactionInfo: signedTransactionInfo,
                productID: productID,
                appGroupID: appGroupID)
        }
        catch
        {
            publish(.failed, message: "The previous purchase entitlement could not be stored.")
            return .unavailable
        }

        guard storedEntitlementIsUnlocked() else
        {
            SharedEntitlement.clear(appGroupID: appGroupID)
            publish(.failed, message: "The previous purchase entitlement could not be validated.")
            return .unavailable
        }

        hasEntitlement = true
        publish(.unlocked)
        return .granted
    }

    private func legacyEnvironmentCanGrant(
        _ environment: AppStore.Environment) -> Bool
    {
        if environment == .production
        {
            return true
        }

#if DEBUG
        return ProcessInfo.processInfo.arguments.contains(
            "-SoundRaveTestLegacyPaidApp")
#else
        return false
#endif
    }

    private func loadProduct() async
    {
        do
        {
            let products = try await Product.products(for: [productID])
            guard let loadedProduct = products.first(where: {
                $0.id == productID && $0.type == .nonConsumable
            }) else
            {
                if !hasEntitlement
                {
                    publish(.failed, message: "The full-version product is not available.")
                }
                return
            }

            product = loadedProduct
            publish(hasEntitlement ? .unlocked : .ready,
                    displayPrice: loadedProduct.displayPrice)
        }
        catch
        {
            if !hasEntitlement
            {
                publish(.failed, message: "Purchase information could not be loaded.")
            }
        }
    }

    private func startTransactionListener()
    {
        listenerTask = Task
        {
            for await verification in Transaction.updates
            {
                if Task.isCancelled
                {
                    return
                }

                switch verification
                {
                    case .verified(let transaction):
                        guard transaction.productID == productID else
                        {
                            continue
                        }

                        if let verified = validated(verification)
                        {
                            grant(verified.transaction,
                                  signedTransactionInfo: verified.signedTransactionInfo)
                        }
                        else
                        {
                            await refreshEntitlement()
                        }

                        await transaction.finish()

                    case .unverified(let transaction, _):
                        if transaction.productID == productID
                        {
                            await refreshEntitlement()
                        }
                }
            }
        }
    }

    private func validated(
        _ verification: VerificationResult<Transaction>)
        -> (transaction: Transaction, signedTransactionInfo: String)?
    {
        guard case .verified(let transaction) = verification,
              transaction.productID == productID,
              transaction.appBundleID == appBundleID,
              transaction.productType == .nonConsumable,
              transaction.revocationDate == nil
        else
        {
            return nil
        }

        let signedTransactionInfo = verification.jwsRepresentation
        let parts = signedTransactionInfo.split(separator: ".", omittingEmptySubsequences: false)
        guard parts.count == 3, parts.allSatisfy({ !$0.isEmpty }) else
        {
            return nil
        }

        return (transaction, signedTransactionInfo)
    }

    private func grant(_ transaction: Transaction, signedTransactionInfo: String)
    {
        do
        {
            try SharedEntitlement.store(
                transaction: transaction,
                signedTransactionInfo: signedTransactionInfo,
                appGroupID: appGroupID)
        }
        catch
        {
            hasEntitlement = false
            SharedEntitlement.clear(appGroupID: appGroupID)
            publish(.failed, message: "The purchase entitlement could not be stored.")
            return
        }

        guard storedEntitlementIsUnlocked() else
        {
            hasEntitlement = false
            SharedEntitlement.clear(appGroupID: appGroupID)
            publish(.failed, message: "The purchase entitlement could not be validated.")
            return
        }

        hasEntitlement = true
        publish(.unlocked)
    }

    private func storedEntitlementIsUnlocked() -> Bool
    {
        return SharedEntitlement.isUnlocked(
            productID: productID,
            appBundleID: appBundleID,
            appGroupID: appGroupID,
            legacyPaidThroughVersion: legacyPaidThroughVersion)
    }

    private func publish(_ state: BridgeState,
                         message: String = "",
                         displayPrice: String? = nil)
    {
        guard let callback else
        {
            return
        }

        let price = displayPrice ?? product?.displayPrice ?? ""
        message.withCString
        {
            messagePointer in
            price.withCString
            {
                pricePointer in
                callback(state.rawValue, messagePointer, pricePointer)
            }
        }
    }
}

@_cdecl("soundrave_storekit_configure")
public func soundraveStoreKitConfigure(_ productIDPointer: UnsafePointer<CChar>?,
                                      _ appBundleIDPointer: UnsafePointer<CChar>?,
                                      _ appGroupIDPointer: UnsafePointer<CChar>?,
                                      _ legacyPaidThroughVersionPointer: UnsafePointer<CChar>?,
                                      _ callback: StoreKitCallback?)
{
    guard let productIDPointer,
          let appBundleIDPointer,
          let appGroupIDPointer,
          let legacyPaidThroughVersionPointer
    else
    {
        return
    }

    let productID = String(cString: productIDPointer)
    let appBundleID = String(cString: appBundleIDPointer)
    let appGroupID = String(cString: appGroupIDPointer)
    let legacyPaidThroughVersion = String(
        cString: legacyPaidThroughVersionPointer)

    Task { @MainActor in
        StoreKitCoordinator.shared.configure(
            productID: productID,
            appBundleID: appBundleID,
            appGroupID: appGroupID,
            legacyPaidThroughVersion: legacyPaidThroughVersion,
            callback: callback)
    }
}

@_cdecl("soundrave_storekit_is_standalone")
public func soundraveStoreKitIsStandalone() -> Int32
{
    return Bundle.main.bundleURL.pathExtension.lowercased() == "appex" ? 0 : 1
}

@_cdecl("soundrave_storekit_purchase")
public func soundraveStoreKitPurchase()
{
    Task { @MainActor in StoreKitCoordinator.shared.purchase() }
}

@_cdecl("soundrave_storekit_restore")
public func soundraveStoreKitRestore()
{
    Task { @MainActor in StoreKitCoordinator.shared.restore() }
}

@_cdecl("soundrave_storekit_refresh")
public func soundraveStoreKitRefresh()
{
    Task { @MainActor in await StoreKitCoordinator.shared.refreshEntitlement() }
}

@_cdecl("soundrave_storekit_read_shared_entitlement")
public func soundraveStoreKitReadSharedEntitlement(
    _ productIDPointer: UnsafePointer<CChar>?,
    _ appBundleIDPointer: UnsafePointer<CChar>?,
    _ appGroupIDPointer: UnsafePointer<CChar>?,
    _ legacyPaidThroughVersionPointer: UnsafePointer<CChar>?) -> Int32
{
    guard let productIDPointer,
          let appBundleIDPointer,
          let appGroupIDPointer,
          let legacyPaidThroughVersionPointer
    else
    {
        return -1
    }

    let productID = String(cString: productIDPointer)
    let appBundleID = String(cString: appBundleIDPointer)
    let appGroupID = String(cString: appGroupIDPointer)
    let legacyPaidThroughVersion = String(
        cString: legacyPaidThroughVersionPointer)

    guard !productID.isEmpty,
          !appBundleID.isEmpty,
          !appGroupID.isEmpty,
          !legacyPaidThroughVersion.isEmpty,
          SharedEntitlement.containerIsAvailable(appGroupID: appGroupID)
    else
    {
        return -1
    }

    return SharedEntitlement.isUnlocked(
        productID: productID,
        appBundleID: appBundleID,
        appGroupID: appGroupID,
        legacyPaidThroughVersion: legacyPaidThroughVersion) ? 1 : 0
}

@_cdecl("soundrave_storekit_update_demo_access")
public func soundraveStoreKitUpdateDemoAccess(
    _ productIDPointer: UnsafePointer<CChar>?,
    _ appBundleIDPointer: UnsafePointer<CChar>?,
    _ appGroupIDPointer: UnsafePointer<CChar>?,
    _ secondsRemainingPointer: UnsafeMutablePointer<Int64>?) -> Int32
{
    guard let productIDPointer,
          let appBundleIDPointer,
          let appGroupIDPointer
    else
    {
        secondsRemainingPointer?.pointee = 0
        return DemoAccessState.unavailable.rawValue
    }

    let productID = String(cString: productIDPointer)
    let appBundleID = String(cString: appBundleIDPointer)
    let appGroupID = String(cString: appGroupIDPointer)
    guard !productID.isEmpty, !appBundleID.isEmpty, !appGroupID.isEmpty else
    {
        secondsRemainingPointer?.pointee = 0
        return DemoAccessState.unavailable.rawValue
    }

    let snapshot = DemoAccess.evaluate(
        productID: productID,
        appBundleID: appBundleID,
        appGroupID: appGroupID)
    secondsRemainingPointer?.pointee = snapshot.secondsRemaining
    return snapshot.state.rawValue
}

#endif
