# Prompt: put the booking page on your website

The booking page can be linked (simplest) or embedded in an iframe on WordPress, Wix, Squarespace,
Shopify or any site.

```
Add online booking to my website <https://www.example.com>, which runs on <WordPress / Wix / Squarespace / Shopify / plain HTML>. The booking system is installed at <https://booking.example.com>.

1. Add a "Book now" button in the main navigation and on the home page that links to <https://booking.example.com/book/>.
2. Also embed the booking page on <https://www.example.com/book> using this HTML:
   <iframe src="https://booking.example.com/book/" title="Book a session" style="width:100%;min-height:900px;border:0" loading="lazy" allow="payment"></iframe>
3. In the booking system's .env set EMBED_ALLOWED_ORIGINS=https://www.example.com (comma separated if more than one) so the page may be framed there, then restart or reload. Without this the browser blocks the iframe.
4. Open the page on desktop and on a phone, complete the booking flow up to the confirmation step without submitting, and take screenshots for me.
5. If the site uses a cookie banner or a page builder that strips iframes, tell me what you found and propose the alternative (link only).

Do not change anything else on the website. Report the URLs of the pages you edited.
```
