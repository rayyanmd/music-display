/// AppleScript sources for Spotify. Each one checks Spotify is running inside the script
/// (so a quitting Spotify is never relaunched) and gives up after 2 s if Spotify hangs.
public enum SpotifyScripts {
  public static let query = wrap(
    """
    set s to player state as string
    if s is "stopped" then return "stopped"
    set t to current track
    set sep to character id 30
    return s & sep & (id of t) & sep & (name of t) & sep & (artist of t) & sep & (album of t) & sep & ((duration of t) as string) & sep & ((player position) as string) & sep & (artwork url of t)
    """, otherwise: "return \"stopped\"")

  public static func command(_ c: DisplayCommand) -> String {
    switch c {
    case .playPause: wrap("playpause", otherwise: "")
    case .next: wrap("next track", otherwise: "")
    case .prev: wrap("previous track", otherwise: "")
    }
  }

  static func wrap(_ body: String, otherwise: String) -> String {
    """
    if application "Spotify" is running then
      with timeout of 2 seconds
        tell application "Spotify"
          \(body)
        end tell
      end timeout
    else
      \(otherwise)
    end if
    """
  }
}
