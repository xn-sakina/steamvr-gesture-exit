# Prioritize Safe Exit Boundaries

The add-on may fail to exit a game rather than risk closing SteamVR, Steam, unrelated tools, or an unverified process. Game Exit is limited to a reliable Exit Target main process: if the target cannot be identified, the Exit Control is not shown, and if closing the target fails, the add-on reports failure without expanding the process scope.
